#ifndef SAS_SNAPSHOT_JSON_H
#define SAS_SNAPSHOT_JSON_H

#include <type_traits>

#include "json.hpp"

// j[key] -> var if j[key] is a number (int/float/double, not bool)
#define SAS_JSON_READ_NUMBER(j, key, var)                          \
    do {                                                           \
        auto sasJsonIt = (j).find(key);                            \
        if (sasJsonIt != (j).end() && sasJsonIt->is_number()) {    \
            (var) = sasJsonIt->get<std::decay_t<decltype(var)>>(); \
        }                                                          \
    } while (0)

// j[key] -> var if j[key] is a boolean
#define SAS_JSON_READ_BOOL(j, key, var)                          \
    do {                                                         \
        auto sasJsonIt = (j).find(key);                          \
        if (sasJsonIt != (j).end() && sasJsonIt->is_boolean()) { \
            (var) = sasJsonIt->get<bool>();                      \
        }                                                        \
    } while (0)

// j[key] -> var if j[key] is a string
#define SAS_JSON_READ_STRING(j, key, var)                       \
    do {                                                        \
        auto sasJsonIt = (j).find(key);                         \
        if (sasJsonIt != (j).end() && sasJsonIt->is_string()) { \
            (var) = sasJsonIt->get<std::string>();              \
        }                                                       \
    } while (0)

// true if j[key] is an array of exactly 3 numbers
#define SAS_JSON_HAS_ARR3(j, key) ((j).contains(key) && (j).at(key).is_array() && (j).at(key).size() == 3)

// j[key] -> arr[0..2] if j[key] is an array of exactly 3 numbers
#define SAS_JSON_READ_ARR3(j, key, arr)                                                           \
    do {                                                                                          \
        auto sasJsonIt = (j).find(key);                                                           \
        if (sasJsonIt != (j).end() && sasJsonIt->is_array() && sasJsonIt->size() == 3) {          \
            bool sasJsonOk = true;                                                                \
            for (int sasJsonI = 0; sasJsonI < 3; ++sasJsonI) {                                    \
                if (!(*sasJsonIt)[sasJsonI].is_number()) {                                        \
                    sasJsonOk = false;                                                            \
                    break;                                                                        \
                }                                                                                 \
            }                                                                                     \
            for (int sasJsonI = 0; sasJsonOk && sasJsonI < 3; ++sasJsonI) {                       \
                (arr)[sasJsonI] = (*sasJsonIt)[sasJsonI].get<std::decay_t<decltype((arr)[0])>>(); \
            }                                                                                     \
        }                                                                                         \
    } while (0)

// j[key] = [a, b, c]
#define SAS_JSON_WRITE_ARR3(j, key, a, b, c)               \
    do {                                                   \
        (j)[key] = nlohmann::json::array({(a), (b), (c)}); \
    } while (0)

#endif // SAS_SNAPSHOT_JSON_H
