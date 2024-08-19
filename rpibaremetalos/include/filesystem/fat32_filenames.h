// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <ctype.h>
#include <minimalcstdlib.h>

#include <fixed_string>
#include <optional>
#include <utility>

#include "filesystem/filesystem_errors.h"

#include "devices/log.h"

namespace filesystems::fat32
{

    //
    //  FAT32 Compact 8.3 Filename
    //

    /**
     * @brief Specifies a structure representing a compact 8.3 filename in the FAT32 file system.
     *
     * This structure is used to store and manipulate filenames in the FAT32 file system.
     * It provides constructors, assignment operators, and comparison operators for working with filenames.
     * The structure also includes member functions for accessing specific properties of the filename.
     */
    struct FAT32Compact8Dot3Filename
    {
        /**
         * @brief Default constructor for FAT32Compact8Dot3Filename.
         */
        FAT32Compact8Dot3Filename() = default;

        /**
         * @brief Constructs a FAT32Compact8Dot3Filename object with the given name and extension.
         *
         * This constructor initializes the FAT32Compact8Dot3Filename object with the provided name and extension.
         * The name and extension are copied into the internal member variables of the object.
         *
         * @param name The name of the file.
         * @param extension The extension of the file.
         */
        FAT32Compact8Dot3Filename(const char *name,
                                  const char *extension)
        {
            for (int i = 0; (i < 8) && (name[i] != 0x00); i++)
            {
                const_cast<char *>(name_)[i] = name[i];
            }

            for (int i = 0; (i < 3) && (extension[i] != 0x00); i++)
            {
                const_cast<char *>(extension_)[i] = extension[i];
            }
        }

        /**
         * @brief Copy constructor for FAT32Compact8Dot3Filename.
         *
         * This constructor creates a new instance of FAT32Compact8Dot3Filename by copying the contents of another instance.
         *
         * @param filename_to_copy The FAT32Compact8Dot3Filename instance to be copied.
         */
        FAT32Compact8Dot3Filename(const FAT32Compact8Dot3Filename &filename_to_copy)
        {
            memcpy(const_cast<char *>(name_), filename_to_copy.name_, 11);
        }

        FAT32Compact8Dot3Filename(FAT32Compact8Dot3Filename &&filename_to_copy) = delete;

        /**
         * @brief Assignment operator for FAT32Compact8Dot3Filename.
         *
         * This operator assigns the value of another FAT32Compact8Dot3Filename object to the current object.
         * It copies the name_ (which includes the extension) of the other object using memcpy.
         *
         * @param filename_to_copy The FAT32Compact8Dot3Filename object to be copied.
         * @return A reference to the current object after the assignment.
         */
        FAT32Compact8Dot3Filename &operator=(const FAT32Compact8Dot3Filename &filename_to_copy)
        {
            memcpy(const_cast<char *>(name_), filename_to_copy.name_, 11);

            return *this;
        }

        FAT32Compact8Dot3Filename &operator=(FAT32Compact8Dot3Filename &&filename_to_copy) = delete;

        /**
         * @brief Overloaded equality operator for comparing FAT32Compact8Dot3Filename objects.
         *
         * This operator compares the name_ member variable of the current object with the name_ member variable of the filename_to_compare object.
         *
         * @param filename_to_compare The FAT32Compact8Dot3Filename object to compare with.
         * @return True if the names are equal, false otherwise.
         */
        bool operator==(const FAT32Compact8Dot3Filename &filename_to_compare) const
        {
            return (memcmp(name_, filename_to_compare.name_, 11) == 0);
        }

        /**
         * @brief Returns the first character of the filename.
         *
         * @return The first character of the filename as a uint8_t.
         */
        uint8_t FirstChar() const
        {
            return static_cast<uint8_t>(name_[0]);
        }

        const char name_[8] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        const char extension_[3] = {0x20, 0x20, 0x20};
    } PACKED;

    //
    //  FAT32 Short Filename
    //

    /**
     * @class FAT32ShortFilename
     * @brief Represents a short filename in the FAT32 file system.
     *
     * The FAT32ShortFilename class provides functionality to handle short filenames in the FAT32 file system.
     * It allows for constructing short filenames from name and extension, extracting components of the filename,
     * checking if the filename is empty, adding a numeric tail to the filename, calculating the checksum of the filename,
     * and more.
     */
    class FAT32ShortFilename
    {
    public:
        static const minstd::fixed_string<64> FORBIDDEN_8_3_FILENAME_CHARACTERS;

        /**
         * Retrieves the permissible character for the given input character.
         *
         * @param current_char The input character to check.
         * @return A pair containing the permissible character and a boolean indicating if the character is permissible.
         */
        static minstd::pair<char, bool> GetPermissibleCharacter(const char current_char);

        /**
         * @brief Default constructor for FAT32ShortFilename.
         */
        FAT32ShortFilename() = default;

        /**
         * @brief Constructs a FAT32ShortFilename object with the given name and extension.
         *
         * @param name The name of the file.
         * @param extension The extension of the file.
         */
        explicit FAT32ShortFilename(const char *name,
                                    const char *extension)
        {
            ScrubFilenameArgument(name);
            ScrubExtensionArgument(extension);

            DetectNumericTail();

            GenerateFilenameWithTailAndCompactFilename();
        }

        /**
         * @brief Constructs a FAT32ShortFilename object with the given name and extension.
         *
         * @param name The name of the file.
         * @param extension The extension of the file.
         */
        explicit FAT32ShortFilename(const minstd::string &name,
                                    const minstd::string &extension)
        {
            ScrubFilenameArgument(name.c_str());
            ScrubExtensionArgument(extension.c_str());

            DetectNumericTail();

            GenerateFilenameWithTailAndCompactFilename();
        }

        /**
         * @brief Constructs a FAT32ShortFilename object from a given FAT32Compact8Dot3Filename.
         *
         * This constructor takes a FAT32Compact8Dot3Filename object and constructs a FAT32ShortFilename object from it.
         * It moves the filename, dropping spaces used for padding and extracts the extension.
         * The compact filename is assumed to be valid and is not scrubbed.
         *
         * @param compact_filename The FAT32Compact8Dot3Filename object to construct the FAT32ShortFilename from.
         */
        explicit FAT32ShortFilename(const FAT32Compact8Dot3Filename &compact_filename)
        {
            const char *src = compact_filename.name_;
            int bytes_copied = 0;

            //  Move the filename, dropping spaces used for padding and extract the extension
            //      We will assume the compact filename is coming off the platter and is OK, so do not scrub the name or extension.

            while ((*src != ' ') && (bytes_copied < 8))
            {
                name_.push_back(*src++);
                bytes_copied++;
            }

            if ((compact_filename.extension_[0] != ' ') || (compact_filename.extension_[1] != ' ') || (compact_filename.extension_[2] != ' '))
            {
                src = compact_filename.extension_;
                bytes_copied = 0;

                while ((*src != ' ') && (bytes_copied < 3))
                {
                    extension_.push_back(*src++);
                    bytes_copied++;
                }
            }

            //  Detect the numeric tail and generate the compact 8.3 filename with the tail

            DetectNumericTail();

            GenerateFilenameWithTailAndCompactFilename();
        }

        /**
         * @brief Constructs a new FAT32ShortFilename object by copying another FAT32ShortFilename object.
         *
         * @param filename_to_copy The FAT32ShortFilename object to be copied.
         */
        explicit FAT32ShortFilename(const FAT32ShortFilename &filename_to_copy)
            : name_(filename_to_copy.name_),
              name_with_tail_(filename_to_copy.name_with_tail_),
              extension_(filename_to_copy.extension_),
              compact8_3_filename_(filename_to_copy.compact8_3_filename_),
              lossy_conversion_(filename_to_copy.lossy_conversion_),
              numeric_tail_(filename_to_copy.numeric_tail_)
        {
        }

        /**
         * @brief Assignment operator for FAT32ShortFilename.
         *
         * This operator assigns the values of another FAT32ShortFilename object to the current object.
         *
         * @param filename_to_copy The FAT32ShortFilename object to be copied.
         * @return A reference to the current object after assignment.
         */
        FAT32ShortFilename &operator=(const FAT32ShortFilename &filename_to_copy)
        {
            name_ = filename_to_copy.name_;
            extension_ = filename_to_copy.extension_;
            name_with_tail_ = filename_to_copy.name_with_tail_;
            compact8_3_filename_ = filename_to_copy.compact8_3_filename_;
            lossy_conversion_ = filename_to_copy.lossy_conversion_;
            numeric_tail_ = filename_to_copy.numeric_tail_;

            return *this;
        }

        /**
         * Returns the name of the file.
         * If a numeric tail exists, the name with the tail is returned.
         * Otherwise, the original name is returned.
         *
         * @return The name of the file.
         */
        const minstd::string &
        Name() const
        {
            //  Append the numeric tail if we have one

            if (numeric_tail_.has_value())
            {
                return name_with_tail_;
            }

            return name_;
        }

        /**
         * @brief Get the extension of the file.
         *
         * This function returns the extension of the file.
         *
         * @return The extension of the file.
         */
        const minstd::string &Extension() const
        {
            return extension_;
        }

        /**
         * @brief Checks if the filename is empty.
         *
         * @return true if the filename is empty, false otherwise.
         */
        const bool IsEmpty() const
        {
            return name_.empty() && extension_.empty();
        }

        /**
         * Returns the numeric tail of the filename.
         *
         * @return The numeric tail of the filename, or an empty optional if it does not exist.
         */
        const minstd::optional<uint32_t> &NumericTail() const
        {
            return numeric_tail_;
        }

        /**
         * Returns the compact 8.3 filename associated with the file.
         *
         * @return The compact 8.3 filename.
         */
        const minstd::string &Compact8_3Filename() const
        {
            return compact8_3_filename_;
        }

        /**
         * Returns whether there was a lossy conversion when creating the filename.
         *
         * @return True if the conversion was lossy, false otherwise.
         */
        bool LossyConversion() const
        {
            return lossy_conversion_;
        }

        /**
         * @brief Checks if the current filename is a derivative of the given basis filename.  A derivative filename is one that
         * matches the basis filename except for the numeric tail.
         *
         * @param basis_filename The basis filename to compare against.
         * @return True if the current filename is a derivative of the basis filename, false otherwise.
         */
        bool IsDerivativeOfBasisFilename(const FAT32ShortFilename &basis_filename) const;

        /**
         * @brief Adds a numeric tail to the filename.
         *
         * This function adds a numeric tail to the filename by appending the given tail value to the existing filename.
         * The tail value must be between 1 and 999999 (inclusive). If the tail value is out of range, the function returns
         * FilesystemResultCodes::FAT32_NUMERIC_TAIL_OUT_OF_RANGE. Otherwise, it updates the numeric_tail_ member variable,
         * generates a new filename with the tail, and returns FilesystemResultCodes::SUCCESS.
         *
         * @param tail The numeric tail value to be added to the filename.
         * @return FilesystemResultCodes The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes AddNumericTail(uint32_t tail)
        {
            if ((tail < 1) || (tail > 999999))
            {
                return FilesystemResultCodes::FAT32_NUMERIC_TAIL_OUT_OF_RANGE;
            }

            numeric_tail_ = tail;

            GenerateFilenameWithTailAndCompactFilename();

            return FilesystemResultCodes::SUCCESS;
        }

        /**
         * Calculates the checksum of the filename in MSDOS format.
         *
         * @return The checksum value.
         */
        uint8_t Checksum() const
        {
            char msdos_format_filename[12] = {' '};

            memset(msdos_format_filename, ' ', 11);

            minstd::fixed_string<12> correct_name(Name());

            memcpy(msdos_format_filename, correct_name.data(), correct_name.length());
            memcpy(msdos_format_filename + 8, extension_.data(), extension_.length());

            uint8_t checksum = 0;

            for (int i = 0; i < 11; i++)
            {
                checksum = ((checksum & 1) ? 0x80 : 0) + (checksum >> 1) + msdos_format_filename[i];
            }

            return checksum;
        }

    private:
        friend class FAT32LongFilename;

        minstd::fixed_string<12> name_;
        minstd::fixed_string<12> name_with_tail_;
        minstd::fixed_string<4> extension_;

        minstd::fixed_string<12> compact8_3_filename_;

        bool lossy_conversion_ = false;

        minstd::optional<uint32_t> numeric_tail_;

        /**
         * @brief Constructs a new FAT32ShortFilename object by copying the contents of another FAT32ShortFilename object.
         *
         * @param filename_to_copy The FAT32ShortFilename object to be copied.
         */
        FAT32ShortFilename(FAT32ShortFilename &filename_to_copy)
            : name_(filename_to_copy.name_),
              name_with_tail_(filename_to_copy.name_with_tail_),
              extension_(filename_to_copy.extension_),
              compact8_3_filename_(filename_to_copy.compact8_3_filename_),
              lossy_conversion_(filename_to_copy.lossy_conversion_),
              numeric_tail_(filename_to_copy.numeric_tail_)
        {
        }

        /**
         * @brief Scrubs the filename argument by removing any characters that are not permissible in a FAT32 filename
         * and sets the lossy conversion falg if any characters are replaced.
         *
         * @param name The filename to be scrubbed.
         */
        void ScrubFilenameArgument(const char *name)
        {
            size_t short_name_length = 0;

            for (uint32_t i = 0; (name[i] != 0) && (short_name_length < 8); i++)
            {
                minstd::pair<char, bool> current_char = GetPermissibleCharacter(name[i]);

                if (current_char.first() != 0)
                {

                    name_.push_back(current_char.first());
                    short_name_length++;
                }

                lossy_conversion_ |= current_char.second();
            }
        }

        /**
         * @brief Scrubs the extension argument by removing any characters that are not permissible in a FAT32 filename extension
         * and sets the lossy conversion flag if any characters are replaced.
         *
         * @param extension The extension argument to be scrubbed.
         */
        void ScrubExtensionArgument(const char *extension)
        {
            size_t short_extension_length = 0;

            for (uint32_t i = 0; (extension[i] != 0) && (short_extension_length < 3); i++)
            {
                minstd::pair<char, bool> current_char = GetPermissibleCharacter(extension[i]);

                if (current_char.first() != 0)
                {
                    extension_.push_back(current_char.first());
                    short_extension_length++;
                }

                lossy_conversion_ |= current_char.second();
            }
        }

        /**
         * Generates a filename with a tail and compact filename.
         */
        void GenerateFilenameWithTailAndCompactFilename();

        /**
         * @brief Detects the numeric tail in the filename.
         *
         * This function is responsible for detecting the numeric tail in the filename.
         * It analyzes the characters at the end of the filename and determines if they
         * form a numeric sequence after a tilda.
         */
        void DetectNumericTail();
    };

    //
    //  FAT32 Long Filename
    //

    /**
     * @class FAT32LongFilename
     * @brief Represents a long filename in the FAT32 file system.
     *
     * The FAT32LongFilename class provides functionality to handle long filenames in the FAT32 file system.
     * It allows construction of a long filename object with the given name, and provides methods to retrieve
     * information about the filename, such as its length and the basis name. It also includes functions to check
     * if the filename is valid and if it is in the 8.3 format. The class ensures that the filename does not contain
     * any forbidden characters and strips leading and trailing spaces and periods from the name.
     */
    class FAT32LongFilename
    {
    public:
        static const minstd::fixed_string<64> FORBIDDEN_LONG_FILENAME_CHARACTERS;

        /**
         * @brief Constructs a FAT32LongFilename object with the given name.
         *
         * @param name The name of the file.
         */
        explicit FAT32LongFilename(const char *name)
            : name_(name)
        {
            StripSpacesAndTrailingPeriods();
        }

        /**
         * @brief Constructs a FAT32LongFilename object with the given name.
         *
         * @param name The name of the file.
         */
        explicit FAT32LongFilename(const minstd::string &name)
            : name_(name)
        {
            StripSpacesAndTrailingPeriods();
        }

        /**
         * @brief Constructs a new FAT32LongFilename object by copying the contents of another FAT32LongFilename object.
         *
         * @param name_to_copy The FAT32LongFilename object to be copied.
         */
        explicit FAT32LongFilename(const FAT32LongFilename &name_to_copy)
            : name_(name_to_copy.name_)
        {
        }

        /**
         * @brief Implicit conversion operator that returns a const char pointer to the name.
         *
         * @return A const char pointer to the name.
         */
        operator const char *() const
        {
            return name_.c_str();
        }

        /**
         * @brief Returns the length of the filename.
         *
         * @return The length of the filename.
         */
        size_t length() const
        {
            return name_.size();
        }

        /**
         * @brief Returns the name of the file.
         *
         * @return The name of the file.
         */
        const minstd::string &Name() const
        {
            return name_;
        }

        /**
         * Checks if the file system is valid.
         *
         * @param error_code  SIDE EFFECT The reference to the variable that will store the error code if the file system is invalid.
         * @return True if the file system is valid, false otherwise.
         */
        bool IsValid(FilesystemResultCodes &error_code) const;

        /**
         * @brief Checks if the given FAT32 short filename is in the 8.3 format.
         *
         * @param short_filename  SIDE EFFECT The FAT32ShortFilename representing the 8.3 compliant filename.
         * @return true if the filename is in the 8.3 format, false otherwise.
         */
        bool Is8Dot3Filename(FAT32ShortFilename &short_filename) const;

        /**
         * Generates the basis filename for the long filename using the rules specified by MSFT.
         *
         * @return The basis name of the file or directory.
         */
        FAT32ShortFilename GetBasisName() const;

    private:
        minstd::fixed_string<MAX_FILENAME_LENGTH + 1> name_; //  One longer than the max length so we catch names that are too long

        /**
         * @brief Checks if the filename contains any forbidden characters.
         *
         * @return true if the filename contains forbidden characters, false otherwise.
         */
        bool ContainsForbiddenCharacter() const
        {
            for (uint32_t i = 0; i < name_.size(); i++)
            {
                if (!isprint(name_[i]) || (FORBIDDEN_LONG_FILENAME_CHARACTERS.find(name_[i]) != minstd::string::npos))
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief Strips leading and trailing spaces and trailing periods from the name.
         *
         * This function removes any leading spaces from the name by repeatedly erasing the first character of the name until a non-space character is encountered.
         * It also removes any trailing spaces or periods from the name by repeatedly removing the last character of the name until a non-space or non-period character is encountered.
         *
         * @note This function modifies the name_ member variable.
         */
        void StripSpacesAndTrailingPeriods()
        {
            while (name_[0] == ' ')
            {
                name_.erase(0, 1);
            }

            while ((name_.back() == ' ') || (name_.back() == '.'))
            {
                name_.pop_back();
            }
        }
    };
} // namespace filesystems::fat32
