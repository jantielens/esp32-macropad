#pragma once

#include <stddef.h>
#include <string.h>

inline bool widget_value_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

inline bool widget_value_equal(const char* left, const char* right) {
    while (widget_value_space(*left)) ++left;
    while (widget_value_space(*right)) ++right;
    size_t left_len = strlen(left), right_len = strlen(right);
    while (left_len && widget_value_space(left[left_len - 1])) --left_len;
    while (right_len && widget_value_space(right[right_len - 1])) --right_len;
    if (left_len != right_len) return false;
    for (size_t index = 0; index < left_len; ++index) {
        char first = left[index], second = right[index];
        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (second >= 'A' && second <= 'Z') second += 'a' - 'A';
        if (first != second) return false;
    }
    return true;
}
