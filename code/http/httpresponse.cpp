/*
 * @Author       : mark
 * @Date         : 2020-06-27
 * @copyleft Apache 2.0
 */ 
#include "httpresponse.h"

using namespace std;

// 表示文件后缀与 MIME 类型之间的映射关系
const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

// HTTP 状态码与状态消息之间的映射关系
const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

//HTTP 状态码与对应错误页面路径之间的映射关系
const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};

//默认构造函数
HttpResponse::HttpResponse() {
    code_ = -1;//初始状态为未定义的状态码
    path_ = srcDir_ = "";
    isKeepAlive_ = false;//默认情况下不保持连接活动状态
    mmFile_ = nullptr; //表示没有分配内存来保存文件内容
    mmFileStat_ = { 0 };//将 mmFileStat_ 结构体的所有成员都设置为0。
};

// 析构函数
HttpResponse::~HttpResponse() {
    UnmapFile();
}

//对 HttpResponse 对象进行初始化
void HttpResponse::Init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    if(mmFile_) { UnmapFile(); }//检查 mmFile_ 是否已分配内存,如果已经分配，则调用 UnmapFile() 函数来取消映射文件
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
}

//根据请求的资源文件生成 HTTP 响应
void HttpResponse::MakeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {//调用 stat 函数来获取请求资源文件的状态信息，并将其存储在 mmFileStat_ 结构体中。如果获取失败（返回值小于0）或者请求的资源是一个目录
        code_ = 404;//将 HTTP 状态码设置为404（表示未找到资源）
    }
    else if(!(mmFileStat_.st_mode & S_IROTH)) {//如果请求的资源文件的权限不允许其他用户读取（S_IROTH 标志未设置）
        code_ = 403;//HTTP 状态码设置为403（表示禁止访问）
    }
    else if(code_ == -1) { //之前未设置状态码（code_ 等于 -1）
        code_ = 200; //将 HTTP 状态码设置为200（表示成功）
    }
    ErrorHtml_();//用于根据状态码生成对应的错误页面内容
    AddStateLine_(buff);//向缓冲区中添加 HTTP 状态行
    AddHeader_(buff);//向缓冲区中添加 HTTP 响应头部
    AddContent_(buff);//向缓冲区中添加 HTTP 响应正文内容
}

char* HttpResponse::File() {
    return mmFile_;
}

size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}

//根据 HTTP 状态码获取相应的错误页面路径，并更新 path_ 变量以及相应的文件状态信息
void HttpResponse::ErrorHtml_() {
    if(CODE_PATH.count(code_) == 1) {
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);//调用 stat 函数获取该路径对应文件的状态信息，将结果存储在 mmFileStat_ 中。
    }
}

//向 HTTP 响应中添加状态行
void HttpResponse::AddStateLine_(Buffer& buff) {
    string status;//用于存储 HTTP 状态消息
    if(CODE_STATUS.count(code_) == 1) {//检查当前的 HTTP 状态码 code_ 是否在 CODE_STATUS 映射表中
        status = CODE_STATUS.find(code_)->second;//获取相应的状态消息
    }
    else {
        code_ = 400;//状态码设置为400
        status = CODE_STATUS.find(400)->second;//获取相应的状态消息。
    }
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");//将 HTTP 状态行添加到缓冲区 buff 中。其中包括了 HTTP 协议版本号、状态码和状态消息。
}

//向 HTTP 响应中添加头部信息
void HttpResponse::AddHeader_(Buffer& buff) {
    buff.Append("Connection: ");
    if(isKeepAlive_) {//检查是否需要保持连接活动状态
        buff.Append("keep-alive\r\n");//添加 Connection: keep-alive 头部
        buff.Append("keep-alive: max=6, timeout=120\r\n");//并且添加 keep-alive: max=6, timeout=120 头部
    } else{
        buff.Append("close\r\n");//添加 Connection: close 头部，表示关闭连接。
    }
    buff.Append("Content-type: " + GetFileType_() + "\r\n");//调用 GetFileType_() 函数获取文件类型，并将其添加到头部中。
}

//向 HTTP 响应中添加内容
void HttpResponse::AddContent_(Buffer& buff) {
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);//使用 open 函数打开请求的资源文件，并获取文件描述符 srcFd。
    if(srcFd < 0) { 
        ErrorContent(buff, "File NotFound!");
        return; 
    }

    /* 将文件映射到内存提高文件的访问速度 
        MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
    LOG_DEBUG("file path %s", (srcDir_ + path_).data());
    int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);//使用 mmap 函数将文件映射到内存中，以提高文件的访问速度。mmap 函数返回映射到内存的起始地址，如果映射失败，则返回 -1。
    if(*mmRet == -1) {
        ErrorContent(buff, "File NotFound!");
        return; 
    }
    mmFile_ = (char*)mmRet;
    close(srcFd);
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

//取消映射之前通过 mmap 函数映射的文件
void HttpResponse::UnmapFile() {
    if(mmFile_) {//确保 mmFile_ 不为空
        munmap(mmFile_, mmFileStat_.st_size);//取消对文件的内存映射
        mmFile_ = nullptr;//取消映射后，文件指针为空
    }
}

//根据请求的文件路径获取文件类型（MIME 类型）
string HttpResponse::GetFileType_() {
    /* 判断文件类型 */
    string::size_type idx = path_.find_last_of('.');//使用 find_last_of 函数找到文件路径中最后一个 '.' 符号的位置，并将其索引存储在变量 idx 中
    if(idx == string::npos) {//如果未找到 '.' 符号
        return "text/plain";
    }
    string suffix = path_.substr(idx);//将后缀截取出来存储在变量 suffix 中。
    if(SUFFIX_TYPE.count(suffix) == 1) {//检查文件后缀是否在 SUFFIX_TYPE 映射表中
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

// 生成 HTTP 响应的错误内容，并将其添加到指定的缓冲区中。
void HttpResponse::ErrorContent(Buffer& buff, string message) 
{
    string body;//用于存储 HTML 错误页面的内容。
    string status;
    body += "<html><title>Error</title>";//向 body 中添加了 HTML 标签，包括标题、背景颜色等。
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1) {//检查当前的 HTTP 状态码 code_ 是否在 CODE_STATUS 映射表中
        status = CODE_STATUS.find(code_)->second;//获取相应的状态消息
    } else {
        status = "Bad Request";//默认为 "Bad Request"
    }
    body += to_string(code_) + " : " + status  + "\n";//将状态码和状态消息添加到 body 中。
    body += "<p>" + message + "</p>";//将传入的 message 参数作为段落添加到 body 中，用于显示错误消息。
    body += "<hr><em>TinyWebServer</em></body></html>";//添加一个水平线和服务器标识信息到 body 中。

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");//使用 buff.Append 方法将 HTTP 头部信息添加到缓冲区 buff 中
    buff.Append(body);//将完整的错误页面内容添加到缓冲区 buff 中
}
