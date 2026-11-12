#include "network/handlers/http/base_api_handler.h"
#include "network/error/error.h"
#include "network/network_service.h"
#include "logger/logger.hpp"

NS_FT_BEGIN

namespace network {

BaseApiHandler::BaseApiHandler(const std::string& path_prefix) 
    : _path_prefix(path_prefix) {
    if (!_path_prefix.empty()) {
        if (_path_prefix[0] != '/') {
            _path_prefix = "/" + _path_prefix;
        }
        if (_path_prefix.back() == '/') {
            _path_prefix.pop_back();
        }
    }
}

void BaseApiHandler::set_service_instances(
    void* quote_parser,
    void* trade_handler, 
    void* strategy_service,
    void* database_manager
) {
    _quote_parser = quote_parser;
    _trade_handler = trade_handler;
    _strategy_service = strategy_service;
    _db_manager = database_manager;
}

void BaseApiHandler::set_network_service(NetworkService* service) {
    _network_service = service;
}

NetworkService* BaseApiHandler::get_network_service() const {
    return _network_service;
}

HttpResponse BaseApiHandler::create_json_response(
    http::status status, 
    const json& body, 
    const HttpRequest& req
) const {
    HttpResponse res{status, req.version()};
    res.set(http::field::server, "FT-Trading-Server");
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.keep_alive(req.keep_alive());
    
    try {
        res.body() = body.dump();
    } catch (const json::exception& e) {
        NETWORK_LOG_ERROR("JSON serialization failed: {}", e.what());
        json error_body = {
            {"code", ErrorCode::INTERNAL_SERVER_ERROR},
            {"message", ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR)},
            {"data", nullptr}
        };
        res.result(http::status::internal_server_error);
        res.body() = error_body.dump();
    }
    
    res.prepare_payload();
    return res;
}

HttpResponse BaseApiHandler::create_error_response(
    http::status status,
    const std::string& error_code,
    const std::string& message,
    const HttpRequest& req,
    const json& data
) const {
    std::string final_message = message.empty() ? ErrorCode::get_message(error_code) : message;
    
    json body = {
        {"code", error_code},
        {"message", final_message},
        {"data", data}
    };
    
    return create_json_response(status, body, req);
}

HttpResponse BaseApiHandler::create_success_response(
    const json& data,
    const HttpRequest& req
) const {
    json body = {
        {"code", ErrorCode::SUCCESS},
        {"message", ErrorCode::get_message(ErrorCode::SUCCESS)},
        {"data", data}
    };
    
    return create_json_response(http::status::ok, body, req);
}

bool BaseApiHandler::parse_json_body(const HttpRequest& req, json& out_json, std::string& error) const {
    try {
        if (req.body().empty()) {
            error = "Empty request body";
            return false;
        }
        
        out_json = json::parse(req.body());
        return true;
        
    } catch (const json::parse_error& e) {
        error = "Invalid JSON format: " + std::string(e.what());
        return false;
    } catch (const json::exception& e) {
        error = "JSON processing error: " + std::string(e.what());
        return false;
    }
}

std::string BaseApiHandler::get_full_path(const std::string& path) const {
    if (_path_prefix.empty()) {
        return path;
    }
    
    std::string full_path = _path_prefix;
    if (!path.empty() && path[0] != '/') {
        full_path += "/";
    }
    full_path += path;
    
    return full_path;
}

} // namespace network

NS_FT_END