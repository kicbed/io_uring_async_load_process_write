#include "baseline/sync_preprocess_baseline.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <cerrno>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace asyncdataloader::baseline {

SyncPreprocessResult run_sync_preprocess_baseline(const SyncPreprocessConfig& config) {
    if (!config.input_path || !config.output_path || config.block_size == 0){
        return SyncPreprocessResult{0, 0, EINVAL};
    }
    // 自定义函数只读保护打开输入输出文件
    auto in_result = util::open_read_only(config.input_path);
    if (in_result.error_number != 0) {
        return SyncPreprocessResult{0, 0, in_result.error_number};
    }
    int fdOutput = ::open(config.output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdOutput < 0){
        return SyncPreprocessResult{0, 0, errno};
    }
    util::FdGuard outGuard(fdOutput);

    //buffer管理操作
    std::vector<char> block(config.block_size);
    std::uint64_t input_offset = 0;
    std::uint64_t output_offset = 0;
    std::uint64_t total_read = 0;
    std::uint64_t total_written = 0;

    while (true) {
        auto read_result = util::read_at(
            in_result.fd.get(),
            block.data(),
            block.size(),
            input_offset
        );

        if (read_result.error_number != 0) {
            return SyncPreprocessResult{total_read, total_written, read_result.error_number};
        }

        if (read_result.bytes_read == 0) {
            break;  // EOF
        }

        // 处理数据
        for (std::size_t i = 0; i < read_result.bytes_read; ++i) {
            if ((block[i] >= 'a' && block[i] <= 'z') ||
                (block[i] >= 'A' && block[i] <= 'Z')) {
                block[i] ^= 0x20;  // 或 block[i] ^= 0x20
            }
        }

        auto write_result = util::write_all_at(
            outGuard.get(),
            block.data(),
            read_result.bytes_read,
            output_offset
        );

        if (write_result.error_number != 0) {
            return SyncPreprocessResult{total_read, total_written, write_result.error_number};
        }

        total_read += read_result.bytes_read;
        total_written += write_result.bytes_written;
        input_offset += read_result.bytes_read;
        output_offset += write_result.bytes_written;
    }

    //写完同步写入持久保存
    auto fsync_result = util::fsync_fd(outGuard.get());
    if (fsync_result.error_number != 0) {
        return SyncPreprocessResult{total_read, total_written, fsync_result.error_number};
    }

    return SyncPreprocessResult{total_read, total_written, 0};
}

}  // namespace asyncdataloader::baseline
