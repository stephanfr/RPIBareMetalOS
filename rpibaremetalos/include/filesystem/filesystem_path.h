// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include "filesystem/filesystem_errors.h"
#include "result.h"

#include <fixed_string>

namespace filesystems
{
    class FilesystemPath
    {
    public:
        class iterator
        {
        public:
            iterator() = delete;

            bool operator==(const iterator &itr_to_compare) const
            {
                return current_offset_ == itr_to_compare.current_offset_;
            }

            bool operator!=(const iterator &itr_to_compare) const
            {
                return current_offset_ != itr_to_compare.current_offset_;
            }

            const char *operator*() const
            {
                return path_.parsed_path_ + current_offset_;
            }

            const char *operator++(int)
            {
                uint32_t starting_offset = current_offset_;

                while ((current_offset_ < path_.length_) && (path_.parsed_path_[current_offset_] != 0x00))
                {
                    current_offset_++;
                }

                if (current_offset_ != path_.length_)
                {
                    current_offset_++;
                }

                return path_.parsed_path_ + starting_offset;
            }

            const char *operator--()
            {
                if (current_offset_ > 1)
                {
                    current_offset_ -= 2;
                }

                while ((current_offset_ > 0) && (path_.parsed_path_[current_offset_] != 0x00))
                {
                    current_offset_--;
                }

                if (path_.parsed_path_[current_offset_] == 0x00)
                {
                    current_offset_++;
                }

                return path_.parsed_path_ + current_offset_;
            }

            minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> FullPath() const
            {
                minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> full_path;

                if (path_.is_root_)
                {
                    full_path = "/";
                    return full_path;
                }

                path_.path_string_.substr(full_path, 0, current_offset_);

                full_path += path_.parsed_path_ + current_offset_;

                return full_path;
            }

        private:
            friend class FilesystemPath;

            const FilesystemPath &path_;

            uint32_t current_offset_;

            iterator(const FilesystemPath &path,
                     uint32_t current_offset)
                : path_(path),
                  current_offset_(current_offset)
            {
            }
        };

        static PointerResult<FilesystemResultCodes, FilesystemPath> ParsePathString(const minstd::string &path_string);

        const minstd::string &FullPath() const
        {
            return path_string_;
        }

        /** @brief Determines if the path is simply the root directory
         *
         *     @return True if path is simply the root directory, false otherwise
         */

        bool IsRoot() const
        {
            return is_root_;
        }

        bool IsRelative() const
        {
            return is_relative_;
        }

        const char *Last() const
        {
            if (is_root_)
            {
                return parsed_path_;
            }

            //  Work backward from the end of the path looking for a null.

            uint32_t i = length_ - 1;

            for (; i > 0; i--)
            {
                if (parsed_path_[i] == 0)
                {
                    break;
                }
            }

            return parsed_path_ + i + 1;
        }

        iterator begin() const
        {
            if (is_relative_)
            {
                return iterator(*this, 0);
            }

            return iterator(*this, 1);
        }

        iterator end() const
        {
            return iterator(*this, length_);
        }

    private:
        friend class iterator;

        static constexpr char DIRECTORY_DELIMITER = '/';

        const minstd::string &path_string_;

        const uint32_t length_;

        bool is_root_ = false;
        bool is_relative_ = false;

        char parsed_path_[MAX_FILESYSTEM_PATH_LENGTH + 2];

        /** @brief Constructs a Path instance with the given string
         *
         *     @param[in] path String containing the path to be parsed
         */

        FilesystemPath(const minstd::string &path)
            : path_string_(path),
              length_(path.length())
        {
            strlcpy(parsed_path_, path_string_.c_str(), path_string_.length() + 1); //  strlcpy adds a final null
        }
    };
} // namespace filesystems
