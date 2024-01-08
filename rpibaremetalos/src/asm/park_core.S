// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


//
//  This function gets the current Exception Level and leaves it in the X0 register.
//

.global ParkCore
ParkCore:

infinite_loop_parking_core:

    wfe
    b       infinite_loop_parking_core
    