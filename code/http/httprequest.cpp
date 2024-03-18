/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "httprequest.h"
using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };

//初始化
void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = ""; //分别表示 HTTP 请求中的方法、路径、版本和请求体，初始化为空字符串。
    state_ = REQUEST_LINE;//设置为 REQUEST_LINE，即请求行状态
    header_.clear();//清空
    post_.clear();
}

// 检查HTTP请求是否是持久连接
bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {//检查请求头中是否包含"Connection"字段
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";//该字段的值为"keep-alive"，并且请求的HTTP版本是"1.1"，则返回true
    }
    return false;
}

// 解析 HTTP 请求
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";//定义了一个 CRLF 字符串，用于表示 HTTP 报文中的换行符
    if(buff.ReadableBytes() <= 0) {
        return false;
    }
    while(buff.ReadableBytes() && state_ != FINISH) {
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);//从缓冲区中提取一个完整的行，并将其存储为一个字符串
        std::string line(buff.Peek(), lineEnd);//从缓冲区中提取的一行数据存储到 line 字符串中
        switch(state_)
        {
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)) {//解析 HTTP 请求行
                return false;
            }
            ParsePath_();//处理路径等信息
            break;    
        case HEADERS:
            ParseHeader_(line);//解析 HTTP 请求头
            if(buff.ReadableBytes() <= 2) {//如果缓冲区中的可读字节数小于等于 2
                state_ = FINISH;//说明请求头部分解析完毕
            }
            break;
        case BODY:
            ParseBody_(line);//解析 HTTP 请求体
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()) { break; } // 如果 lineEnd 的位置等于缓冲区的写指针位置
        buff.RetrieveUntil(lineEnd + 2);// 从缓冲区中移除已经处理过的数据
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());//日志记录
    return true;
}

//解析 HTTP 请求中的路径
void HttpRequest::ParsePath_() {
    if(path_ == "/") {//如果路径是根路径 "/"
        path_ = "/index.html"; //将其设置为默认的首页路径 "/index.html"
    }
    else {
        for(auto &item: DEFAULT_HTML) {//在默认 HTML 页面列表中
            if(item == path_) {
                path_ += ".html";//将其后缀设置为 ".html"
                break;
            }
        }
    }
}

// 解析HTTP请求行
bool HttpRequest::ParseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");//正则表达式用于匹配HTTP请求行
    smatch subMatch;//用于保存正则表达式的匹配结果
    if(regex_match(line, subMatch, patten)) {   
        method_ = subMatch[1];//方法
        path_ = subMatch[2];//路径
        version_ = subMatch[3];//HTTP版本
        state_ = HEADERS;//状态设置为解析请求头部
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

// 解析 HTTP 请求头部
void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];//将键值对存储到 header_ 中
    }
    else {
        state_ = BODY;//将键值对存储到 header_ 中
    }
}

// 解析 HTTP 请求体
void HttpRequest::ParseBody_(const string& line) {
    body_ = line;//请求体内容存储到 body_ 中
    ParsePost_();//解析 POST 请求的参数
    state_ = FINISH;//将状态更新为 FINISH
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());//记录日志
}

int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

// 解析 HTTP POST 请求中的表单数据
void HttpRequest::ParsePost_() {
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {//检查请求方法是否为 POST，以及请求头中的 Content-Type 是否为 "application/x-www-form-urlencoded"
        ParseFromUrlencoded_();//解析表单数据
        if(DEFAULT_HTML_TAG.count(path_)) {//检查路径是否在默认 HTML 标签列表中
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                } 
                else {
                    path_ = "/error.html";
                }
            }
        }
    }   
}

// 解析 URL 编码的表单数据
void HttpRequest::ParseFromUrlencoded_() {
    if(body_.size() == 0) { return; }//检查 body_ 是否为空

    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

    for(; i < n; i++) {//遍历整个 body_，对每个字符进行处理
        char ch = body_[i];
        switch (ch) {
        case '=':
            key = body_.substr(j, i - j);//表示键值对的键名结束，接着开始解析值
            j = i + 1;
            break;
        case '+':
            body_[i] = ' ';//将其替换为空格；
            break;
        case '%'://表示接下来两个字符是十六进制数，需要将其转换成字符；
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&'://表示键值对的值结束，将键值对插入到 post_ 中，并输出调试信息；
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j < i) {//将最后一个键值对插入到 post_ 中
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

// 用于验证用户身份或注册用户
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;
    SqlConnRAII(&sql,  SqlConnPool::Instance());
    assert(sql);
    
    bool flag = false;//标识查询结果是否符合预期
    unsigned int j = 0;//记录查询结果集中的字段数量
    char order[256] = { 0 };//存储 SQL 查询语句的缓冲区
    MYSQL_FIELD *fields = nullptr;//存储查询结果集中的字段信息
    MYSQL_RES *res = nullptr;//用于存储查询结果集
    
    if(!isLogin) { flag = true; }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) { //执行 SQL 查询语句
        mysql_free_result(res);//释放 res 指向的查询结果集内存
        return false; 
    }
    res = mysql_store_result(sql);//释放 res 指向的查询结果集内存
    j = mysql_num_fields(res);//获取查询结果集中的字段数量，将结果保存在 j 中
    fields = mysql_fetch_fields(res);//获取查询结果集中的字段信息，并将结果保存在 fields 中

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } 
        else { 
            flag = false; 
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { 
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

// 获取POST请求中特定键对应的值
std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");//确保传入的键不为空
    if(post_.count(key) == 1) {//确保传入的键不为空
        return post_.find(key)->second;//返回对应的值
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}