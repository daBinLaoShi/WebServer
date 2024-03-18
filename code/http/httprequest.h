/*
 * @Author       : mark
 * @Date         : 2020-06-25
 * @copyleft Apache 2.0
 */ 
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class HttpRequest {
public:
    //HTTP 请求解析的状态
    enum PARSE_STATE {
        REQUEST_LINE,//表示正在解析 HTTP 请求行。
        HEADERS,//表示正在解析 HTTP 请求头。
        BODY,//表示正在解析 HTTP 请求体。
        FINISH,//表示解析完成。
    };

    //表示 HTTP 请求处理的结果。
    enum HTTP_CODE {
        NO_REQUEST = 0,//表示 HTTP 请求处理的结果。
        GET_REQUEST,//表示成功收到一个 GET 请求。
        BAD_REQUEST,//表示收到了一个无法解析的请求。
        NO_RESOURSE,//表示请求的资源不存在。
        FORBIDDENT_REQUEST,//表示请求被拒绝，通常是因为权限不足。
        FILE_REQUEST,//表示请求的是一个文件。
        INTERNAL_ERROR,//表示服务器内部发生错误。
        CLOSED_CONNECTION,//表示连接已关闭。
    };
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /* 
    todo 
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    PARSE_STATE state_; // 用于表示 HTTP 请求的解析状态
    std::string method_, path_, version_, body_; //存储 HTTP 请求中的方法、路径、版本和请求体
    std::unordered_map<std::string, std::string> header_;//存储 HTTP 请求的头部信息
    std::unordered_map<std::string, std::string> post_;//用于存储 HTTP POST 请求的数据

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
    static int ConverHex(char ch);
};


#endif //HTTP_REQUEST_H