#include "RedisClient.h"


RedisClient::RedisClient() {}

RedisClient::RedisClient(std::string host, int port)
{
    this->connect(host, port);
}

RedisClient::~RedisClient()
{
    redisFree(context);
}

void RedisClient::connect(std::string host, int port)
{
    struct timeval timeout = {1, 500000}; // 1.5 seconds
    context = redisConnectWithTimeout(host.c_str(), port, timeout);
}

bool RedisClient::set(const char * key, size_t key_size, const char * value, size_t value_size)
{
    auto reply = static_cast<redisReply *>(redisCommand(context, "SET %b %b", key, key_size, value, value_size));
    freeReplyObject(reply);
    return true;
}

std::shared_ptr<redisReply> RedisClient::get(char * key, size_t key_size)
{
    auto reply = static_cast<redisReply *>(redisCommand(context, "GET %b", key, key_size));
    return std::shared_ptr<redisReply>(reply, [](redisReply * r) { freeReplyObject(r); });
}
