// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


//
//  This function gets the current Exception Level and leaves it in the X0 register as the return value.
//

.global GetExceptionLevel
GetExceptionLevel:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    ret
