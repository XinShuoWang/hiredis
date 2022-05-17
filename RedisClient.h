#pragma once

#include <memory>
#include <string>
#include "hiredis.h"

class RedisClient
{
public:
    RedisClient();
    RedisClient(std::string host, int port);
    ~RedisClient();
    void connect(std::string, int);
    bool set(const char *, size_t, const char *, size_t);
    std::shared_ptr<redisReply> get(char *, size_t);
    std::shared_ptr<redisReply> keys(char *, size_t);

private:
    redisContext * context;
};
