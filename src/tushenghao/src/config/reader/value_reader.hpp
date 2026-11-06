#pragma once
#include <opencv2/core.hpp>
#include <cmath>
#include <string>

namespace mark::config_value
{
    enum class NumericPolicy {
        NodeNumber, CompleteNumericString
    };
    enum class Error {
        Missing, String, Integer, Bool01, Number, PartialNumber, NonFinite
    };

    // Q02：只共享节点原语；模块显式提供数值/空值策略与抛错上下文；Fail 必须抛出异常。
    template<class Fail>
    cv::FileNode required(const cv::FileNode &node, const char *key, Fail fail)
    {
        const auto value = node[key];
        if (value.empty())
            fail(Error::Missing);
        return value;
    }

    template<class Fail>
    std::string text(const cv::FileNode &node, Fail fail)
    {
        if (!node.isString())
            fail(Error::String);
        return static_cast<std::string>(node);
    }

    template<class Fail>
    int integer(const cv::FileNode &node, Fail fail)
    {
        if (!node.isInt())
            fail(Error::Integer);
        return static_cast<int>(node);
    }

    template<class Fail>
    bool boolean(const cv::FileNode &node, Fail fail)
    {
        const auto value = integer(node, fail);
        if (value != 0 && value != 1)
            fail(Error::Bool01);
        return value == 1;
    }

    template<class Fail>
    double number(const cv::FileNode &node, NumericPolicy policy, Fail fail)
    {
        double value = 0;
        if (node.isInt() || node.isReal())
            value = static_cast<double>(node);
        else if (policy == NumericPolicy::CompleteNumericString && node.isString())
        {
            const auto token = static_cast<std::string>(node);
            size_t used = 0;
            value = std::stod(token, &used);
            if (used != token.size())
                fail(Error::PartialNumber);
        }
        else
            fail(Error::Number);
        if (!std::isfinite(value))
            fail(Error::NonFinite);
        return value;
    }

}
