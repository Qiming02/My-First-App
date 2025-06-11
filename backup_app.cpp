#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <openssl/md5.h>
#include <algorithm>

namespace fs = std::filesystem;

class BackupApp {
public:
    struct FileInfo {
        fs::path path;
        std::string md5;
        uintmax_t size;
        time_t last_modified;
    };

    struct BackupInfo {
        std::string timestamp;
        fs::path backup_path;
        size_t file_count;
        size_t copied_files;
        fs::path source_dir;
        bool is_incremental;
        std::string based_on;
    };

    std::vector<BackupInfo> backup_history;

    // 计算文件的MD5哈希值
    std::string calculate_md5(const fs::path& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            throw std::runtime_error("无法打开文件: " + filepath.string());
        }

        MD5_CTX md5Context;
        MD5_Init(&md5Context);

        char buffer[1024 * 16];
        while (file.good()) {
            file.read(buffer, sizeof(buffer));
            MD5_Update(&md5Context, buffer, file.gcount());
        }

        unsigned char result[MD5_DIGEST_LENGTH];
        MD5_Final(result, &md5Context);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto c : result) {
            oss << std::setw(2) << (int)c;
        }

        return oss.str();
    }

    // 扫描目录并返回文件信息
    std::vector<FileInfo> scan_directory(const fs::path& dir_path) {
        std::vector<FileInfo> files_info;
        
        for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                try {
                    FileInfo info;
                    info.path = entry.path();
                    info.md5 = calculate_md5(entry.path());
                    info.size = entry.file_size();
                    info.last_modified = fs::last_write_time(entry.path()).time_since_epoch().count();
                    
                    files_info.push_back(info);
                } catch (const std::exception& e) {
                    std::cerr << "无法处理文件 " << entry.path() << ": " << e.what() << std::endl;
                }
            }
        }
        
        return files_info;
    }

    // 获取当前时间戳
    std::string current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf;
        localtime_r(&in_time_t, &tm_buf);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        return ss.str();
    }

    // 创建完整备份
    bool create_backup(const fs::path& source_dir, const fs::path& backup_dir) {
        if (!fs::exists(source_dir)) {
            std::cerr << "错误：源目录不存在!" << std::endl;
            return false;
        }

        // 创建备份目录
        std::string timestamp = current_timestamp();
        fs::path current_backup_dir = backup_dir / ("backup_" + timestamp);
        fs::create_directories(current_backup_dir);

        std::cout << "正在扫描源目录: " << source_dir << std::endl;
        auto source_files = scan_directory(source_dir);

        std::cout << "正在复制文件..." << std::endl;
        size_t copied_files = 0;
        for (const auto& file_info : source_files) {
            fs::path relative_path = fs::relative(file_info.path, source_dir);
            fs::path dest_path = current_backup_dir / relative_path;
            
            fs::create_directories(dest_path.parent_path());
            
            try {
                fs::copy_file(file_info.path, dest_path, fs::copy_options::overwrite_existing);
                copied_files++;
            } catch (const std::exception& e) {
                std::cerr << "无法复制文件 " << relative_path << ": " << e.what() << std::endl;
            }
        }

        // 记录备份历史
        BackupInfo backup_info;
        backup_info.timestamp = timestamp;
        backup_info.backup_path = current_backup_dir;
        backup_info.file_count = source_files.size();
        backup_info.copied_files = copied_files;
        backup_info.source_dir = source_dir;
        backup_info.is_incremental = false;
        backup_history.push_back(backup_info);

        // 保存备份历史到文件
        fs::path history_file = backup_dir / "backup_history.txt";
        std::ofstream history(history_file, std::ios::app);
        if (history) {
            history << timestamp << ": 备份自 " << source_dir << " (共 " 
                   << copied_files << "/" << source_files.size() << " 文件)\n";
        }

        std::cout << "\n备份完成! 保存到: " << current_backup_dir << std::endl;
        std::cout << "共处理 " << copied_files << "/" << source_files.size() << " 个文件" << std::endl;
        return true;
    }

    // 创建增量备份
    bool incremental_backup(const fs::path& source_dir, const fs::path& backup_dir) {
        if (!fs::exists(source_dir)) {
            std::cerr << "错误：源目录不存在!" << std::endl;
            return false;
        }

        // 获取最新备份
        std::vector<fs::path> backups;
        for (const auto& entry : fs::directory_iterator(backup_dir)) {
            if (entry.is_directory() && entry.path().filename().string().find("backup_") == 0) {
                backups.push_back(entry.path());
            }
        }

        if (backups.empty()) {
            std::cout << "没有找到之前的备份，将执行完整备份" << std::endl;
            return create_backup(source_dir, backup_dir);
        }

        // 按时间排序找到最新备份
        std::sort(backups.begin(), backups.end());
        fs::path latest_backup = backups.back();
        std::cout << "找到最新备份: " << latest_backup << std::endl;

        // 扫描源目录和最新备份
        std::cout << "正在扫描文件变更..." << std::endl;
        auto source_files = scan_directory(source_dir);
        auto backup_files = scan_directory(latest_backup);

        // 找出需要备份的文件(新增或修改的)
        std::vector<FileInfo> files_to_backup;
        for (const auto& source_file : source_files) {
            fs::path relative_path = fs::relative(source_file.path, source_dir);
            
            auto it = std::find_if(backup_files.begin(), backup_files.end(), 
                [&relative_path](const FileInfo& info) {
                    return fs::relative(info.path, info.path.parent_path().parent_path()) == relative_path;
                });
            
            if (it == backup_files.end() || source_file.md5 != it->md5) {
                files_to_backup.push_back(source_file);
            }
        }

        if (files_to_backup.empty()) {
            std::cout << "没有发现需要备份的文件变更!" << std::endl;
            return false;
        }

        // 创建增量备份
        std::string timestamp = current_timestamp();
        fs::path current_backup_dir = backup_dir / ("backup_" + timestamp);
        fs::create_directories(current_backup_dir);

        std::cout << "正在备份 " << files_to_backup.size() << " 个变更文件..." << std::endl;
        size_t copied_files = 0;
        for (const auto& file_info : files_to_backup) {
            fs::path relative_path = fs::relative(file_info.path, source_dir);
            fs::path dest_path = current_backup_dir / relative_path;
            
            fs::create_directories(dest_path.parent_path());
            
            try {
                fs::copy_file(file_info.path, dest_path, fs::copy_options::overwrite_existing);
                copied_files++;
            } catch (const std::exception& e) {
                std::cerr << "无法复制文件 " << relative_path << ": " << e.what() << std::endl;
            }
        }

        // 从旧备份复制未修改的文件(创建硬链接节省空间)
        std::cout << "处理未修改的文件..." << std::endl;
        for (const auto& backup_file : backup_files) {
            fs::path relative_path = fs::relative(backup_file.path, latest_backup);
            fs::path dest_path = current_backup_dir / relative_path;
            
            bool need_copy = true;
            for (const auto& source_file : source_files) {
                if (fs::relative(source_file.path, source_dir) == relative_path && 
                    source_file.md5 == backup_file.md5) {
                    need_copy = false;
                    break;
                }
            }
            
            if (need_copy) {
                fs::create_directories(dest_path.parent_path());
                try {
                    // 尝试创建硬链接
                    fs::create_hard_link(backup_file.path, dest_path);
                } catch (...) {
                    // 硬链接失败则复制文件
                    try {
                        fs::copy_file(backup_file.path, dest_path, fs::copy_options::overwrite_existing);
                    } catch (const std::exception& e) {
                        std::cerr << "无法复制文件 " << relative_path << ": " << e.what() << std::endl;
                    }
                }
            }
        }

        // 记录备份历史
        BackupInfo backup_info;
        backup_info.timestamp = timestamp;
        backup_info.backup_path = current_backup_dir;
        backup_info.file_count = source_files.size();
        backup_info.copied_files = copied_files;
        backup_info.source_dir = source_dir;
        backup_info.is_incremental = true;
        backup_info.based_on = latest_backup.filename().string();
        backup_history.push_back(backup_info);

        fs::path history_file = backup_dir / "backup_history.txt";
        std::ofstream history(history_file, std::ios::app);
        if (history) {
            history << timestamp << ": 增量备份自 " << source_dir << " (共 " 
                   << copied_files << "/" << files_to_backup.size() << " 变更文件)\n";
        }

        std::cout << "\n增量备份完成! 保存到: " << current_backup_dir << std::endl;
        std::cout << "共处理 " << copied_files << " 个变更文件" << std::endl;
        return true;
    }

    void show_backup_history() {
        if (backup_history.empty()) {
            std::cout << "没有备份历史记录" << std::endl;
            return;
        }

        std::cout << "\n=== 备份历史 ===" << std::endl;
        for (const auto& backup : backup_history) {
            std::cout << "时间: " << backup.timestamp << std::endl;
            std::cout << "类型: " << (backup.is_incremental ? "增量备份" : "完整备份") << std::endl;
            if (backup.is_incremental) {
                std::cout << "基于: " << backup.based_on << std::endl;
            }
            std::cout << "源目录: " << backup.source_dir << std::endl;
            std::cout << "备份位置: " << backup.backup_path << std::endl;
            std::cout << "文件数: " << backup.copied_files << "/" << backup.file_count << std::endl;
            std::cout << "------------------------" << std::endl;
        }
    }

    void show_menu() {
        std::cout << "\n=== 数据备份应用 ===" << std::endl;
        std::cout << "1. 完整备份" << std::endl;
        std::cout << "2. 增量备份" << std::endl;
        std::cout << "3. 查看备份历史" << std::endl;
        std::cout << "4. 退出" << std::endl;
    }

    void run() {
        std::cout << "欢迎使用数据备份应用" << std::endl;

        while (true) {
            show_menu();
            std::cout << "请选择操作 (1-4): ";
            std::string choice;
            std::getline(std::cin, choice);

            if (choice == "1") {
                std::cout << "请输入要备份的源目录路径: ";
                std::string source;
                std::getline(std::cin, source);
                
                std::cout << "请输入备份目标目录路径: ";
                std::string target;
                std::getline(std::cin, target);
                
                create_backup(source, target);
            } else if (choice == "2") {
                std::cout << "请输入要备份的源目录路径: ";
                std::string source;
                std::getline(std::cin, source);
                
                std::cout << "请输入备份目标目录路径: ";
                std::string target;
                std::getline(std::cin, target);
                
                incremental_backup(source, target);
            } else if (choice == "3") {
                show_backup_history();
            } else if (choice == "4") {
                std::cout << "感谢使用，再见!" << std::endl;
                break;
            } else {
                std::cout << "无效选择，请重新输入!" << std::endl;
            }
        }
    }
};

int main() {
    // 初始化OpenSSL MD5
    OpenSSL_add_all_digests();

    BackupApp app;
    app.run();

    // 清理OpenSSL
    EVP_cleanup();
    return 0;
}
