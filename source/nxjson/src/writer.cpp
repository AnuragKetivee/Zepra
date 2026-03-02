/**
 * @file writer.cpp
 * @brief NxJSON serialization/writer implementation
 */

#include "nxjson.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>

static void write_indent(std::string& out, int depth, int indent_size) {
    for (int i = 0; i < depth * indent_size; i++) {
        out += ' ';
    }
}

static void write_string(std::string& out, const char* str, size_t len, bool escape_unicode) {
    out += '"';
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else if (escape_unicode && (unsigned char)c >= 0x80) {
                    // Escape non-ASCII as \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

static void write_value(std::string& out, const NxJsonValue* value, 
                        const NxJsonWriteOptions* opts, int depth);

static void write_array(std::string& out, const NxJsonValue* value,
                        const NxJsonWriteOptions* opts, int depth) {
    out += '[';
    size_t count = nx_json_array_size(value);
    
    if (count == 0) {
        out += ']';
        return;
    }
    
    bool pretty = opts && opts->pretty;
    int indent = opts ? opts->indent_size : 2;
    
    for (size_t i = 0; i < count; i++) {
        if (pretty) {
            out += '\n';
            write_indent(out, depth + 1, indent);
        }
        write_value(out, nx_json_array_get(value, i), opts, depth + 1);
        if (i + 1 < count) out += ',';
    }
    
    if (pretty) {
        out += '\n';
        write_indent(out, depth, indent);
    }
    out += ']';
}

static void write_object(std::string& out, const NxJsonValue* value,
                         const NxJsonWriteOptions* opts, int depth) {
    out += '{';
    size_t count = nx_json_object_count(value);
    
    if (count == 0) {
        out += '}';
        return;
    }
    
    bool pretty = opts && opts->pretty;
    int indent = opts ? opts->indent_size : 2;
    bool escape_unicode = opts && opts->escape_unicode;
    
    // Collect keys for optional sorting
    std::vector<size_t> indices(count);
    for (size_t i = 0; i < count; i++) indices[i] = i;
    
    if (opts && opts->sort_keys) {
        std::sort(indices.begin(), indices.end(), [value](size_t a, size_t b) {
            NxJsonMember ma = nx_json_object_at(value, a);
            NxJsonMember mb = nx_json_object_at(value, b);
            return strcmp(ma.key, mb.key) < 0;
        });
    }
    
    for (size_t i = 0; i < count; i++) {
        NxJsonMember m = nx_json_object_at(value, indices[i]);
        
        if (pretty) {
            out += '\n';
            write_indent(out, depth + 1, indent);
        }
        
        write_string(out, m.key, strlen(m.key), escape_unicode);
        out += ':';
        if (pretty) out += ' ';
        write_value(out, m.value, opts, depth + 1);
        
        if (i + 1 < count) out += ',';
    }
    
    if (pretty) {
        out += '\n';
        write_indent(out, depth, indent);
    }
    out += '}';
}

static void write_value(std::string& out, const NxJsonValue* value,
                        const NxJsonWriteOptions* opts, int depth) {
    if (!value) {
        out += "null";
        return;
    }
    
    bool escape_unicode = opts && opts->escape_unicode;
    
    switch (nx_json_type(value)) {
        case NX_JSON_NULL:
            out += "null";
            break;
            
        case NX_JSON_BOOL:
            out += nx_json_get_bool(value) ? "true" : "false";
            break;
            
        case NX_JSON_NUMBER: {
            double num = nx_json_get_number(value);
            char buf[64];
            // Check if it's an integer
            if (num == (int64_t)num && num >= -9007199254740992.0 && num <= 9007199254740992.0) {
                snprintf(buf, sizeof(buf), "%lld", (long long)num);
            } else {
                snprintf(buf, sizeof(buf), "%.17g", num);
            }
            out += buf;
            break;
        }
        
        case NX_JSON_STRING:
            write_string(out, nx_json_get_string(value), nx_json_get_string_len(value), escape_unicode);
            break;
            
        case NX_JSON_ARRAY:
            write_array(out, value, opts, depth);
            break;
            
        case NX_JSON_OBJECT:
            write_object(out, value, opts, depth);
            break;
    }
}

extern "C" {

char* nx_json_stringify(const NxJsonValue* value, const NxJsonWriteOptions* options) {
    std::string out;
    out.reserve(256);
    write_value(out, value, options, 0);
    
    char* result = static_cast<char*>(malloc(out.size() + 1));
    if (result) {
        memcpy(result, out.c_str(), out.size() + 1);
    }
    return result;
}

char* nx_json_to_string(const NxJsonValue* value) {
    return nx_json_stringify(value, nullptr);
}

} // extern "C"
