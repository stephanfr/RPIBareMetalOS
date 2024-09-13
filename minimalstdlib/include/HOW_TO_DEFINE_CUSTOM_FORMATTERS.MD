<!--
 Copyright 2024 Stephan Friedl. All rights reserved.
 Use of this source code is governed by a BSD-style
 license that can be found in the LICENSE file.
-->

Custom formatters can be defined by supplying 3 declarations and the right implementations.  A custom formatter must
provide:

1.  A class exposing an 'fmt_arg' interface (found in the 'format_arg' header)
2.  An instance of 'inline const size_t GetFmtArgSize(type value)' returning the size of the formatter
3.  An instance of 'inline const fmt_arg *GetFmtArg(void *mem_loc, type value)' returning an argument for the value

If a custom formatter uses the standard arg_format_options, then the 'DECLARE_TYPE_FORMATTER' macro in the
'format_formatters' header may be used and only an implementation of the 'AppendInternal()' method must
be supplied.  Alternatively the completely unspecialized version of 'fmt_arg_base' may be used with
a custom format options type -or- a completely custom instance can be created simply implementing
the 'fmt_arg' virtual method.

It *should* be possible to define a formatter for just about any type with the ability to define a fully
custom formattin option specification - with the exception that the formatting option MUST begin with
a position specifier or simply a colon.

It is probably best to simply retain a reference to whatever the format argument may be, the supplied classes
do that.  Formatters are declared on the stack so declaring one that is 'big' will chew up stack space.
