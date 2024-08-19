// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <ctype.h>

#include "filesystem/fat32_filenames.h"

#include "devices/log.h"

namespace filesystems::fat32
{
    //
    //  FAT32ShortFilename
    //

    const minstd::fixed_string<64> FAT32ShortFilename::FORBIDDEN_8_3_FILENAME_CHARACTERS("\"*/:<>?\\|+,;=[]");

    minstd::pair<char, bool> FAT32ShortFilename::GetPermissibleCharacter(const char current_char)
    {
        if (isalpha(current_char))
        {
            return minstd::make_pair((char)toupper(current_char), false);
        }
        else if (isspace(current_char) || (current_char == '.'))
        {
            //  Document says strip all leading and embedded spaces - so I assume embedded includes trailing.
            //      Similarly, doc says strip leading periods but also do not copy any periods in the long filename

            return minstd::make_pair((char)0, false);
        }
        else if ((FORBIDDEN_8_3_FILENAME_CHARACTERS.find(current_char) == minstd::string::npos) &&
                 (current_char > 31) &&
                 (current_char != 127))
        {
            //  The current character is a permissible 8.3 filename character, so add it to the short filename

            return minstd::make_pair(current_char, false);
        }

        //  The current character is impermissible, so replace it with an underscore and return true to indicate a lossy conversion

        return minstd::make_pair('_', true);
    }

    void FAT32ShortFilename::DetectNumericTail()
    {
        numeric_tail_.reset();

        //  Examine characters from the back of the filename moving to the front.  If all chars are
        //      numeric until we hit a '~' character, then there is a numeric tail.

        uint32_t front_of_number;

        for (front_of_number = name_.length() - 1; front_of_number > 0; front_of_number--)
        {
            if (!isdigit(name_[front_of_number]))
            {
                break;
            }
        }

        if ((front_of_number == name_.length() - 1) || (name_[front_of_number] != '~'))
        {
            return;
        }

        numeric_tail_ = strtol(&(name_[front_of_number + 1]), NULL, 10, NULL);
        name_.erase(front_of_number);
    }

    bool FAT32ShortFilename::IsDerivativeOfBasisFilename(const FAT32ShortFilename &basis_filename) const
    {
        //  Extensions must match

        if (extension_ != basis_filename.extension_)
        {
            return false;
        }

        //  Both must have a tail or not

        if (basis_filename.NumericTail().has_value() != NumericTail().has_value())
        {
            return false;
        }

        //  If there is no tail, then the names must match exactly

        if (!numeric_tail_.has_value())
        {
            return name_ == basis_filename.name_;
        }

        //  Characters in this filename must match those of the basis filename until we hit the tilda character.

        uint32_t current_char = 0;

        while ((name_with_tail_[current_char] != 0) && (basis_filename.name_with_tail_[current_char] != 0))
        {
            if (name_with_tail_[current_char] == '~')
            {
                return true;
            }

            if (name_with_tail_[current_char] != basis_filename.name_with_tail_[current_char])
            {
                return false;
            }

            current_char++;
        }

        //  We should never end up down here - the name with tail is guaranteed to have a tilda character

        return false;
    }

    void FAT32ShortFilename::GenerateFilenameWithTailAndCompactFilename()
    {
        if (numeric_tail_.has_value())
        {
            name_with_tail_ = name_;

            char buffer[32];

            itoa(numeric_tail_.value(), buffer, 10);

            if ((name_with_tail_.size() + strnlen(buffer, 32) + 1) > 8)
            {
                uint32_t chars_to_erase = (name_with_tail_.size() + strnlen(buffer, 32) + 1) - 8;

                name_with_tail_.erase(name_with_tail_.size() - chars_to_erase);
            }

            name_with_tail_ += "~";
            name_with_tail_ += buffer;
        }

        //  Now for the 8.3 filename

        compact8_3_filename_ = Name().c_str();

        if (!extension_.empty())
        {
            compact8_3_filename_ += ".";
            compact8_3_filename_ += extension_.c_str();
        }
    }

    //
    //  FAT32LongFilename
    //

    const minstd::fixed_string<64> FAT32LongFilename::FORBIDDEN_LONG_FILENAME_CHARACTERS("<>:\"/\\|?*");

    bool FAT32LongFilename::IsValid(FilesystemResultCodes &error_code) const
    {
        if (name_.empty())
        {
            error_code = FilesystemResultCodes::EMPTY_FILENAME;
            return false;
        }

        if (name_.size() > MAX_FILENAME_LENGTH)
        {
            error_code = FilesystemResultCodes::FILENAME_TOO_LONG;
            return false;
        }

        if (ContainsForbiddenCharacter())
        {
            error_code = FilesystemResultCodes::FILENAME_CONTAINS_FORBIDDEN_CHARACTERS;
            return false;
        }

        return true;
    }

    bool FAT32LongFilename::Is8Dot3Filename(FAT32ShortFilename &short_filename) const
    {
        //  Reset the short filename

        short_filename = FAT32ShortFilename();

        //  If the name is greater than 12 characters, then it cannot be an 8.3 filename

        if (name_.size() > 12)
        {
            return false;
        }

        //  Scan for any illegal characters, including lower case characters

        for (uint32_t i = 0; i < name_.size(); i++)
        {
            if (islower(name_[i]) || (FAT32ShortFilename::FORBIDDEN_8_3_FILENAME_CHARACTERS.find(name_[i]) != minstd::string::npos))
            {
                return false;
            }
        }

        //  Look for a period

        size_t extension_location = name_.find_last_of('.');

        //  We know there are no illegal characters, the name is 8.3 if there are less than 8 characters in the name

        if (extension_location == minstd::string::npos)
        {
            if (name_.size() <= 8)
            {
                short_filename = FAT32ShortFilename(name_, "");
                return true;
            }
        }

        //  If there is a period, then there must be no more than one period

        if (name_.find('.') != extension_location)
        {
            return false;
        }

        //  If there is a period, then the name must be less than 8 characters and the extension must be less than 3 characters

        if (extension_location > 8)
        {
            return false;
        }

        if (name_.size() - extension_location > 4)
        {
            return false;
        }

        //  If we made it here, then this is an 8.3 filename, so set the short filename and return true

        minstd::fixed_string<> name;
        minstd::fixed_string<> extension;

        name_.substr(name, 0, extension_location);
        name_.substr(extension, extension_location + 1, name_.size() - extension_location - 1);

        short_filename = FAT32ShortFilename(name, extension);

        return true;
    }

    FAT32ShortFilename FAT32LongFilename::GetBasisName() const
    {
        LogEntryAndExit("Entering with name: %s\n", name_.c_str());

        //  Format of the Basis Name is documented in MSFT's FAT Specification.
        //      We are not working with UCS2 characters, so the OEM conversion does not apply.

        FAT32ShortFilename short_filename;

        //  Find the start of the extension

        size_t extension_location = name_.find_last_of('.');

        //  Convert the long filename to uppercase

        for (uint32_t i = 0; i < minstd::min(name_.size(), extension_location) && short_filename.name_.size() < 8; i++)
        {
            minstd::pair<char, bool> compliant_char = FAT32ShortFilename::GetPermissibleCharacter(name_[i]);

            //  If the compliant char is zero, then it is character we should skip and set the lossy conversion flag

            if (compliant_char.first() == 0)
            {
                short_filename.lossy_conversion_ = true;
                continue;
            }

            //  Append the compliant char

            short_filename.name_.push_back(compliant_char.first());

            //  If we got an underscore back and the character was not originally an underscore, then we have a lossy conversion

            short_filename.lossy_conversion_ = short_filename.lossy_conversion_ | compliant_char.second();
        }

        //  We should have an uppercase 8 character filename without leading periods and no spaces

        //  Search backward from the end of the long filename for a period.  The first 3 characters after the period will be the extension.

        if (extension_location != minstd::string::npos)
        {
            for (uint32_t i = extension_location + 1; i < name_.size() && short_filename.extension_.size() < 3; i++)
            {
                minstd::pair<char, bool> compliant_char = FAT32ShortFilename::GetPermissibleCharacter(name_[i]);

                //  If the compliant char is zero, then it is character we should skip and set the lossy conversion flag

                if (compliant_char.first() == 0)
                {
                    short_filename.lossy_conversion_ = true;
                    continue;
                }

                //  Append the compliant char

                short_filename.extension_.push_back(compliant_char.first());

                //  If we got an underscore back and the character was not originally an underscore, then we have a lossy conversion

                short_filename.lossy_conversion_ = short_filename.lossy_conversion_ | compliant_char.second();
            }
        }

        //  If this has been a lossy conversion or if the long filename does not fit into an 8.3 - add a numeric tail.

        if (short_filename.lossy_conversion_ ||
            (minstd::min(name_.size(), extension_location) > 8) ||
            ((extension_location != minstd::string::npos) && (name_.size() - (extension_location + 1)) > 3))
        {
            short_filename.AddNumericTail(1);
        }

        //  Finished with Success

        return short_filename;
    }
} // namespace filesystems::fat32
