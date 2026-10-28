#pragma once

#include <string>

using String = std::string;
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT };
struct HTTPUpload {};
struct HTTPRaw {};
class WebServer;

// The subset of the pinned WebServer handler interface used by the production
// guard. Selection precedes multipart upload, just as in Parsing.cpp.
class RequestHandler {
 public:
    virtual ~RequestHandler() = default;
    virtual bool canHandle(HTTPMethod, String) { return false; }
    virtual bool canUpload(String) { return false; }
    virtual bool canRaw(String) { return false; }
    virtual bool handle(WebServer&, HTTPMethod, String) { return false; }
    virtual void upload(WebServer&, String, HTTPUpload&) {}
    virtual void raw(WebServer&, String, HTTPRaw&) {}
};

class WebServer {
 public:
    int status = 0;
    void send(int code, const char*, const char*) { status = code; }
};
