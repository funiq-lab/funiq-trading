#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>

// 添加跨平台文件操作的头文件
#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
    #include <io.h>
    #define mkdir(path, mode) _mkdir(path)
    #define access _access
    #define F_OK 0
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <dirent.h>
#endif

// 获取文件路径的父目录
inline std::string get_parent_path(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

// 递归创建目录
inline bool create_directories(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    
    // 检查目录是否已存在
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // 递归创建父目录
    std::string parent = get_parent_path(path);
    if (parent != path && !create_directories(parent)) {
        return false;
    }
    
    // 创建当前目录
    return mkdir(path.c_str(), 0755) == 0;
}

// 检查文件或目录是否存在
inline bool file_exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

// 删除文件
inline bool remove_file(const std::string& path) {
    return remove(path.c_str()) == 0;
}

// 获取文件扩展名
inline std::string get_extension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(pos);
}

// 获取文件名（不含路径）
inline std::string get_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

// 检查是否为常规文件
inline bool is_regular_file(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

// 列出目录中的所有文件
inline std::vector<std::string> list_directory(const std::string& dir_path) {
    std::vector<std::string> files;
    
#ifdef _WIN32
    WIN32_FIND_DATA findFileData;
    std::string search_path = dir_path + "/*";
    HANDLE hFind = FindFirstFile(search_path.c_str(), &findFileData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = findFileData.cFileName;
            if (filename != "." && filename != "..") {
                files.push_back(dir_path + "/" + filename);
            }
        } while (FindNextFile(hFind, &findFileData) != 0);
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(dir_path.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename != "." && filename != "..") {
                files.push_back(dir_path + "/" + filename);
            }
        }
        closedir(dir);
    }
#endif
    
    return files;
}