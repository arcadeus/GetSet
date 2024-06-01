#include <filesystem>
#include "simpleini/SimpleIni.h"
#include "cpp-httplib/httplib.h"
using namespace httplib;

//
static void build_html(Response& res, const std::string& title, const std::string& body)
{
    res.set_content(
        "<!DOCTYPE html>\n"
        "<html>\n"
        "    <head>\n"
        "        <title>GetSet - " + title + "</title>\n"
        "        <meta charset='utf-8' />\n"
        "    </head>\n"
        "    <body>\n"
        "        <h1>" + title + "</h1>\n" + body + "\n"
        "    </body>\n"
        "</html>\n",
        "text/html"
    );
}

//
static void error_400(Response& res, const std::string& msg)
{
    res.status = 400;
    res.reason = "Bad Request";
    build_html(
        res,
        "400 Bad Request",
        "        <span style='color:red'>" + msg + "</span>"
    );
}

//
// Server's lifetime data
//
class GetSet
{    
    std::string m_www_path;

    //
    // У сервера есть конфигурационный файл, он лежит на диске, config.txt, в нем хранитятся данные ключ/значение,
    //
    std::string m_config_path;
    CSimpleIniA m_config;

    //
    // Read/write statistics
    //
    using one_stat_t = std::pair<int, int>; // number of read + writes
    using stat_t = std::map<std::string, one_stat_t>; // key => its stat
    stat_t m_stat;

    //
    // Protect SimpleIni library & m_stat
    //
    std::mutex m_mutex;

    //
    // Call under m_mutex protection
    //
    static std::string build_response(const std::string& name, const std::string& value, const one_stat_t& stat)
    {
        return name + "=" + value
            + " (reads=" + std::to_string(stat.first)
            + ", writes=" + std::to_string(stat.second)
            + ")";

    }

    //
    void process_get(const std::string& name, Response& res)
    {
        std::string value;
        std::string response;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            value = m_config.GetValue("main", name.c_str(), "");

            one_stat_t& stat = m_stat[name];
            stat.first++;

            response = build_response(name, value, stat);
        }

        res.set_content(response, "text/plain");
    }

    //
    void process_set(const std::string& params, Response& res)
    {
        const size_t pos = params.find('=');
        if (pos == std::string::npos)
            return error_400(res, "Failed to parse SET params: " + params);

        const std::string& name  = params.substr(0, pos);
        const std::string& value = params.substr(pos + 1);

        std::string response;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config.SetValue("main", name.c_str(), value.c_str());
            m_config.SaveFile(m_config_path.c_str());

            one_stat_t& stat = m_stat[name];
            stat.second++;

            response = build_response(name, value, stat);
        }

        res.set_content("<span style='background:#fdd'>SET " + response + "</span>", "text/plain");
    }

    //
    void process_command(const std::string& cmd, Response& res)
    {
        constexpr size_t PREFIX_LENGTH = 5;

        if (cmd.length() > PREFIX_LENGTH)
        {
            const std::string& type   = cmd.substr(0, PREFIX_LENGTH);
            const std::string& params = cmd.substr(PREFIX_LENGTH);

            if (type == "$get ")
                return process_get(params, res);

            if (type == "$set ")
                return process_set(params, res);
        }

        error_400(res, "Failed to parse command: " + cmd);
    }

public:
    //
    void process_GET(Response& res)
    {
        error_400(res, "Use POST method");
    }

    //
    void process_POST(const Request& req, Response& res)
    {
        if (!req.has_param("command"))
            return error_400(res, "No command POSTed");

        const std::string& cmd = req.get_param_value("command");
        process_command(cmd, res);
    }

    //
    const std::string& get_www_path() const
    {
        return m_www_path;
    }

    //
    explicit GetSet()
    {
        std::filesystem::path path;

        // Get path to executable
    #ifdef _WIN32
        wchar_t szPath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, szPath, MAX_PATH);
        path  = szPath;
    #else
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        path  = std::string(result, (count > 0) ? count : 0);
    #endif

        // Build path to www/ & config file
        m_www_path =
        m_config_path = path.parent_path();
        m_www_path.append("/www");
        m_config_path.append("/config.txt");

        // Open config file with simpleini library
        const SI_Error rc = m_config.LoadFile(m_config_path.c_str());
        if (rc < 0)
        {
            std::cerr << "Failed to read " << m_config_path << ": " + std::to_string(rc) << "\n";
            exit(2);
        }
        assert(rc == SI_OK);
    }
};

//
int main()
{
    GetSet getset;
    Server svr;

    //
    // Home page
    //
    svr.Get("/", [&](const Request&, Response& res) {
        build_html(
            res,
            "Home",
            "        <ul>\n"
            "            <li><a href='/manual.html'>Manual test</a></li>\n"
            "            <li><a href='/auto.html'>Auto test</a></li>\n"
            "            <li><a href='/command'>Command URL (GET gives <code>400 Bad request</code>)</a></li>\n"
            "        </ul>"
        );
    });

    //
    // Other GET stuff
    //
    auto ret = svr.set_mount_point("/", getset.get_www_path());
    if (!ret)
    {
        std::cerr << "Static mount failed\n";
        exit(1);
    }

    //
    // Commands handling
    //
    svr.Get("/command", [&](const Request&, Response& res) {
        getset.process_GET(res);
    });

    svr.Post("/command", [&](const Request& req, Response& res) {
        getset.process_POST(req, res);
    });

    svr.listen("localhost", 8081);
}
