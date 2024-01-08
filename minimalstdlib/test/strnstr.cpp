// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stddef.h>

char	*strnstr(const char *string_to_search, const char *string_to_search_for, size_t max_count)
{
	unsigned long	i = 0;
	int				j = 0;

    if (!*string_to_search_for)
    {
		return ((char *)string_to_search);
    }

	while (string_to_search[i])
	{
		j = 0;

		while (string_to_search[i] == string_to_search_for[j] && string_to_search[i] && i < max_count)
		{
			i++;
			j++;
		}

		if (!string_to_search_for[j])
        {
			return ((char *)&string_to_search[i - j]);
        }

        i = (i - j) + 1;
	}

	return NULL;
}
