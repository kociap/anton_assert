#include <debugging.hpp>

#define _NO_CVCONST_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
// Windows.h must be included before DbgHelp.h. Otherwise we get undefined identifiers in DbgHelp.h
#include <DbgHelp.h>

#include <new>
#include <string.h> // memset

// Specifies symbol's basic type.
// Copied from cvconst.h
//
// https://docs.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/basictype
//
enum BasicType {
    btNoType = 0,
    btVoid = 1,
    btChar = 2,
    btWChar = 3,
    btInt = 6,
    btUInt = 7,
    btFloat = 8,
    btBCD = 9,
    btBool = 10,
    btLong = 13,
    btULong = 14,
    btCurrency = 25,
    btDate = 26,
    btVariant = 27,
    btComplex = 28,
    btBit = 29,
    btBSTR = 30,
    btHresult = 31,
    btChar16 = 32, // char16_t
    btChar32 = 33, // char32_t
    btChar8 = 34,  // char8_t
};

// Describes the variety of User-Defined Types (UDT)
// Copied from cvconst.h
//
// https://docs.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/udtkind?view=vs-2019
//
enum UdtKind {
    UdtStruct,
    UdtClass,
    UdtUnion,
    UdtInterface // unused
};

[[nodiscard]] static uint64_t get_last_error() {
    return GetLastError();
}

[[nodiscard]] static int32_t convert_utf16_to_utf8(char16_t const* buffer_utf16, char* buffer_utf8) {
    if (char16_t high_surrogate = *buffer_utf16; high_surrogate > 0xD7FF && high_surrogate < 0xE000) {
        // Surrogate pair
        uint16_t low_surrogate = *(buffer_utf16 + 1);
        char32_t codepoint = (((high_surrogate & 0x3FF) >> 10) | (low_surrogate & 0x3FF)) + 0x10000;
        buffer_utf8[0] = 0xF0 | ((codepoint & 0x1C0000) >> 18);
        buffer_utf8[1] = 0x80 | ((codepoint & 0x3F000) >> 12);
        buffer_utf8[2] = 0x80 | ((codepoint & 0xFC0) >> 6);
        buffer_utf8[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    } else {
        char16_t codepoint = *buffer_utf16;
        if (codepoint <= 0x7F) {
            *buffer_utf8 = codepoint;
            return 1;
        } else if (codepoint <= 0x7FF) {
            buffer_utf8[0] = 0xC0 | ((codepoint & 0x7C0) >> 6);
            buffer_utf8[1] = 0x80 | (codepoint & 0x3F);
            return 2;
        } else {
            buffer_utf8[0] = 0xE0 | ((codepoint & 0xF000) >> 12);
            buffer_utf8[1] = 0x80 | ((codepoint & 0xFC0) >> 6);
            buffer_utf8[2] = 0x80 | (codepoint & 0x3F);
            return 3;
        }
    }
}

[[nodiscard]] static std::string string_from_utf16(char16_t const* str) {
    char codepoint_utf8[5] = {};
    std::string string;
    while (*str != char16_t(0)) {
        int32_t bytes_written = convert_utf16_to_utf8(str, codepoint_utf8);
        string.append(std::string(codepoint_utf8, bytes_written));
        str += (bytes_written < 4 ? 1 : 2);
    }
    return string;
}

[[nodiscard]] static std::string process_base_type(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    ULONG64 length = 0;
    if (!SymGetTypeInfo(process, module_base, index, TI_GET_LENGTH, &length)) {
        [[maybe_unused]] uint64_t const error_code = get_last_error();
        return {u8"unknown_type"};
    }

    DWORD base_type = 0;
    if (!SymGetTypeInfo(process, module_base, index, TI_GET_BASETYPE, &base_type)) {
        [[maybe_unused]] uint64_t const error_code = get_last_error();
        return {u8"unknown_type"};
    }

    switch (base_type) {
        case btVoid:
            return {u8"void"};
        case btBool:
            return {u8"bool"};
        case btChar:
            return {u8"char"};
        case btWChar:
            return {u8"wchar_t"};
        case btChar8:
            return {u8"char8_t"};
        case btChar16:
            return {u8"char16_t"};
        case btChar32:
            return {u8"char32_t"};
        case btInt:
            if (length == 1) {
                return {u8"char"};
            } else if (length == 2) {
                return {u8"short"};
            } else if (length == 8) {
                return {u8"long long"};
            } else {
                return {u8"int"};
            }
        case btUInt:
            if (length == 1) {
                return {u8"unsigned char"};
            } else if (length == 2) {
                return {u8"unsigned short"};
            } else if (length == 8) {
                return {u8"unsigned long long"};
            } else {
                return {u8"unsigned int"};
            }
        case btLong:
            return {u8"long"};
        case btULong:
            return {u8"unsigned long"};
        case btFloat:
            if (length == 0 || length == 4) {
                return {u8"float"};
            } else {
                return {u8"double"};
            }

        default:
            return {u8"unknown_type"};
    }
}

[[nodiscard]] static std::string process_typedef(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    if (WCHAR* typedef_name = nullptr; SymGetTypeInfo(process, module_base, index, TI_GET_SYMNAME, &typedef_name)) {
        std::string name = string_from_utf16(reinterpret_cast<char16_t*>(typedef_name));
        LocalFree(typedef_name);
        return name;
    } else {
        return {u8"unknown_typedef"};
    }
}

[[nodiscard]] static std::string process_pointer_type(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    bool is_reference = false;
    // For some bizzare reason `process` changes after the call to SymGetTypeInfo,
    // so we make a copy of the valid value and use it instead.
    HANDLE const prcs = process;
    if (!SymGetTypeInfo(process, module_base, index, TI_GET_IS_REFERENCE, &is_reference)) {
        [[maybe_unused]] uint64_t error_code = get_last_error();
    }

    if (DWORD type_index = 0; SymGetTypeInfo(prcs, module_base, index, TI_GET_TYPEID, &type_index)) {
        std::string subtype = get_type_as_string(prcs, module_base, type_index);
        if (is_reference) {
            subtype.append(u8"&");
        } else {
            subtype.append(u8"*");
        }
        return subtype;
    } else {
        [[maybe_unused]] uint64_t error_code = get_last_error();
        if (is_reference) {
            return {u8"unknown_type&"};
        } else {
            return {u8"unknown_type*"};
        }
    }
}

[[nodiscard]] static std::string process_function_type(HANDLE const process, ULONG64 const module_base, ULONG64 const index, char const* name = nullptr) {
    std::string type_string;

    // Return type
    if (DWORD return_type_index = 0; SymGetTypeInfo(process, module_base, index, TI_GET_TYPEID, &return_type_index)) {
        std::string const return_type = get_type_as_string(process, module_base, return_type_index);
        type_string.append(return_type);
    } else {
        [[maybe_unused]] uint64_t const error_code = get_last_error();
        type_string.append(u8"unknown_type");
    }

    if (name != nullptr) {
        type_string.append(u8" ");
        type_string.append(name);
    }

    // Parameters
    if (DWORD parameters_count = 0; SymGetTypeInfo(process, module_base, index, TI_GET_CHILDRENCOUNT, &parameters_count)) {
        uint64_t alloc_size = sizeof(TI_FINDCHILDREN_PARAMS) + sizeof(ULONG) * parameters_count;
        TI_FINDCHILDREN_PARAMS& children_params =
            *reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(::operator new(alloc_size, static_cast<std::align_val_t>(alignof(TI_FINDCHILDREN_PARAMS))));
        memset(&children_params, 0, alloc_size);
        children_params.Count = parameters_count;
        if (SymGetTypeInfo(process, module_base, index, TI_FINDCHILDREN, &children_params)) {
            type_string.append(u8"(");
            for (uint64_t i = children_params.Start; i < children_params.Count; ++i) {
                std::string const argument = get_type_as_string(process, module_base, children_params.ChildId[i]);
                type_string.append(argument);
                if (i + 1 < children_params.Count) {
                    type_string.append(u8", ");
                }
            }
            type_string.append(u8")");
        } else {
            [[maybe_unused]] uint64_t const error_code = get_last_error();
        }
    } else {
        [[maybe_unused]] uint64_t const error_code = get_last_error();
    }

    return type_string;
}

[[nodiscard]] static std::string process_function(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    if (DWORD type_index = 0; SymGetTypeInfo(process, module_base, index, TI_GET_TYPEID, &type_index)) {
        if (WCHAR* fn_name_wchar = nullptr; SymGetTypeInfo(process, module_base, index, TI_GET_SYMNAME, &fn_name_wchar)) {
            std::string function_name = string_from_utf16(reinterpret_cast<char16_t*>(fn_name_wchar));
            LocalFree(fn_name_wchar);
            return process_function_type(process, module_base, type_index, function_name.data());
        } else {
            [[maybe_unused]] uint64_t const error_code = get_last_error();
            return process_function_type(process, module_base, type_index, u8"unknown_name");
        }
    } else {
        return {u8"unknown_function"};
    }
}

[[nodiscard]] static std::string process_function_argument(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    if (DWORD type_index = 0; SymGetTypeInfo(process, module_base, index, TI_GET_TYPEID, &type_index)) {
        return get_type_as_string(process, module_base, type_index);
    } else {
        [[maybe_unused]] uint64_t error_code = get_last_error();
        return {u8"unknown_type"};
    }
}

[[nodiscard]] static std::string process_class(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    if (WCHAR* name = nullptr; SymGetTypeInfo(process, module_base, index, TI_GET_SYMNAME, &name)) {
        std::string class_name = string_from_utf16(reinterpret_cast<char16_t*>(name));
        LocalFree(name);
        return class_name;
    } else {
        [[maybe_unused]] uint64_t const error_code = get_last_error();
        return {u8"unknown_name"};
    }
}

[[nodiscard]] static std::string process_udt(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    if (DWORD udt_kind = ~0; SymGetTypeInfo(process, module_base, index, TI_GET_UDTKIND, &udt_kind)) {
        switch (udt_kind) {
            case UdtStruct:
            case UdtClass:
            case UdtUnion:
                // Since we are only getting the name of the UDT, all 3 cases are the same.
                return process_class(process, module_base, index);
            default:
                return {u8"unknown_type"};
        }
    } else {
        [[maybe_unused]] uint64_t error_code = get_last_error();
        return {u8"unknown_type"};
    }
}

std::string get_type_as_string(HANDLE const process, ULONG64 const module_base, ULONG64 const index) {
    DWORD type_tag = 0;
    if (!SymGetTypeInfo(process, module_base, index, TI_GET_SYMTAG, &type_tag)) {
        [[maybe_unused]] uint64_t const error_message = get_last_error();
        return {};
    }

    switch (type_tag) {
        case SymTagBaseType:
            return process_base_type(process, module_base, index);

        case SymTagTypedef:
            return process_typedef(process, module_base, index);

        case SymTagPointerType:
            return process_pointer_type(process, module_base, index);

        case SymTagFunctionType:
            return process_function_type(process, module_base, index);

        case SymTagFunction:
            return process_function(process, module_base, index);

        case SymTagFunctionArgType:
            return process_function_argument(process, module_base, index);

        case SymTagUDT:
            return process_udt(process, module_base, index);

        default:
            return {u8"unknown_type"};
    }
}
