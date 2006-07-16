/*

Copyright (C) 2004 David Bateman
Copyright (C) 1998-2004 Andy Adler

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

Octave is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

*/

#if !defined (octave_oct_spparms_h)
#define octave_oct_spparms_h 1

#include <cassert>
#include <cstddef>

#include <iostream>

#include "str-vec.h"
#include "dColVector.h"
#include "dNDArray.h"

#define OCTAVE_SPARSE_CONTROLS_SIZE 12

class
octave_sparse_params
{
protected:

  octave_sparse_params (void)
    : params (OCTAVE_SPARSE_CONTROLS_SIZE),
      keys (OCTAVE_SPARSE_CONTROLS_SIZE) 
  {
    init_keys ();
    do_defaults ();
  }

public:

  static bool instance_ok (void);

  static void defaults (void);

  static void tight (void);
  
  static string_vector get_keys (void);

  static ColumnVector get_vals (void);

  static bool set_vals (const NDArray& vals);

  static bool set_key (const std::string& key, const double& val);

  static double get_key (const std::string& key);

  static void print_info (std::ostream& os, const std::string& prefix);

private:

  ColumnVector params;

  string_vector keys;

  static octave_sparse_params *instance;

  void do_defaults (void);

  void do_tight (void);
  
  string_vector do_get_keys (void) const { return keys; }

  ColumnVector do_get_vals (void) const { return params; }

  bool do_set_vals (const NDArray& vals);

  bool do_set_key (const std::string& key, const double& val);

  double do_get_key (const std::string& key);

  void do_print_info (std::ostream& os, const std::string& prefix) const;
  
  void init_keys (void);
};

#endif

/*
;;; Local Variables: ***
;;; mode: C++ ***
;;; End: ***
*/
