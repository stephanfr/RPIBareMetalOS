/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>


char *__utoa( unsigned int value, char *str, int base )
{
	const char* numericConversionDigits = "0123456789abcdefghijklmnopqrstuvwxyz";

	unsigned int	remainder;
	unsigned int    i;
	int             j;
	char            c;
  
    //  Insure the desired base is legal, only 2 to 36 is supported

    if((base < 2) || (base > 36))
    { 
        str[0] = '\0';
        return NULL;
    }  
    
    //  Digits will be in reverse order as we convert

    i = 0;

    do 
    {
        remainder = value % base;
        str[i++] = numericConversionDigits[remainder];
        value = value / base;
    }
    while (value != 0);
    
    str[i] = '\0'; 

	//	Reverse the string in place

	for (j = 0, i--; j < i; j++, i--)
	{
		c = str[j];
		str[j] = str[i];
		str[i] = c; 
	}       
  
	return str;
}

char *utoa( unsigned int value, char *str, int base )
{
  return __utoa (value, str, base);
}
