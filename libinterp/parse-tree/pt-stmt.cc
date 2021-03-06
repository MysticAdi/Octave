/*

Copyright (C) 1996-2017 John W. Eaton

This file is part of Octave.

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Octave is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<http://www.gnu.org/licenses/>.

*/

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <typeinfo>

#include "quit.h"

#include "bp-table.h"
#include "defun.h"
#include "error.h"
#include "errwarn.h"
#include "input.h"
#include "oct-lvalue.h"
#include "octave-link.h"
#include "ov.h"
#include "pager.h"
#include "pt-bp.h"
#include "pt-cmd.h"
#include "pt-id.h"
#include "pt-idx.h"
#include "pt-jump.h"
#include "pt-pr-code.h"
#include "pt-stmt.h"
#include "pt-walk.h"
#include "unwind-prot.h"
#include "utils.h"
#include "variables.h"

namespace octave
{
  // A list of commands to be executed.

  tree_statement::~tree_statement (void)
  {
    delete cmd;
    delete expr;
    delete comm;
  }

  void
  tree_statement::set_print_flag (bool print_flag)
  {
    if (expr)
      expr->set_print_flag (print_flag);
  }

  bool
  tree_statement::print_result (void)
  {
    return expr && expr->print_result ();
  }

  void
  tree_statement::set_breakpoint (const std::string& condition)
  {
    if (cmd)
      cmd->set_breakpoint (condition);
    else if (expr)
      expr->set_breakpoint (condition);
  }

  void
  tree_statement::delete_breakpoint (void)
  {
    if (cmd)
      cmd->delete_breakpoint ();
    else if (expr)
      expr->delete_breakpoint ();
  }

  bool
  tree_statement::is_breakpoint (bool check_active) const
  {
    return cmd ? cmd->is_breakpoint (check_active)
      : (expr ? expr->is_breakpoint (check_active) : false);
  }

  std::string
  tree_statement::bp_cond () const
  {
    return cmd ? cmd->bp_cond () : (expr ? expr->bp_cond () : "0");
  }

  int
  tree_statement::line (void) const
  {
    return cmd ? cmd->line () : (expr ? expr->line () : -1);
  }

  int
  tree_statement::column (void) const
  {
    return cmd ? cmd->column () : (expr ? expr->column () : -1);
  }

  void
  tree_statement::set_location (int l, int c)
  {
    if (cmd)
      cmd->set_location (l, c);
    else if (expr)
      expr->set_location (l, c);
  }

  void
  tree_statement::echo_code (void)
  {
    tree_print_code tpc (octave_stdout, VPS4);

    accept (tpc);
  }

  bool
  tree_statement::is_end_of_fcn_or_script (void) const
  {
    bool retval = false;

    if (cmd)
      {
        tree_no_op_command *no_op_cmd
          = dynamic_cast<tree_no_op_command *> (cmd);

        if (no_op_cmd)
          retval = no_op_cmd->is_end_of_fcn_or_script ();
      }

    return retval;
  }

  bool
  tree_statement::is_end_of_file (void) const
  {
    bool retval = false;

    if (cmd)
      {
        tree_no_op_command *no_op_cmd
          = dynamic_cast<tree_no_op_command *> (cmd);

        if (no_op_cmd)
          retval = no_op_cmd->is_end_of_file ();
      }

    return retval;
  }

  tree_statement *
  tree_statement::dup (symbol_table::scope_id scope,
                       symbol_table::context_id context) const
  {
    tree_statement *new_stmt = new tree_statement ();

    new_stmt->cmd = (cmd ? cmd->dup (scope, context) : 0);

    new_stmt->expr = (expr ? expr->dup (scope, context) : 0);

    new_stmt->comm = (comm ? comm->dup () : 0);

    return new_stmt;
  }

  // Create a "breakpoint" tree-walker, and get it to "walk" this statement list
  // (FIXME: What does that do???)
  int
  tree_statement_list::set_breakpoint (int line, const std::string& condition)
  {
    tree_breakpoint tbp (line, tree_breakpoint::set, condition);
    accept (tbp);

    return tbp.get_line ();
  }

  void
  tree_statement_list::delete_breakpoint (int line)
  {
    if (line < 0)
      {
        octave_value_list bp_lst = list_breakpoints ();

        int len = bp_lst.length ();

        for (int i = 0; i < len; i++)
          {
            tree_breakpoint tbp (i, tree_breakpoint::clear);
            accept (tbp);
          }
      }
    else
      {
        tree_breakpoint tbp (line, tree_breakpoint::clear);
        accept (tbp);
      }
  }

  octave_value_list
  tree_statement_list::list_breakpoints (void)
  {
    tree_breakpoint tbp (0, tree_breakpoint::list);
    accept (tbp);

    return tbp.get_list ();
  }

  // Get list of pairs (breakpoint line, breakpoint condition)
  std::list<bp_type>
  tree_statement_list::breakpoints_and_conds (void)
  {
    tree_breakpoint tbp (0, tree_breakpoint::list);
    accept (tbp);

    std::list<bp_type> retval;
    octave_value_list lines = tbp.get_list ();
    octave_value_list conds = tbp.get_cond_list ();

    for (int i = 0; i < lines.length (); i++)
      {
        retval.push_back (bp_type (lines(i).double_value (),
                                   conds(i).string_value ()));
      }

    return retval;
  }

  // Add breakpoints to  file  at multiple lines (the second arguments of  line),
  // to stop only if  condition  is true.
  // Updates GUI via  octave_link::update_breakpoint.
  // FIXME: COME BACK TO ME.
  bp_table::intmap
  tree_statement_list::add_breakpoint (const std::string& file,
                                       const bp_table::intmap& line,
                                       const std::string& condition)
  {
    bp_table::intmap retval;

    octave_idx_type len = line.size ();

    for (int i = 0; i < len; i++)
      {
        bp_table::const_intmap_iterator p = line.find (i);

        if (p != line.end ())
          {
            int lineno = p->second;

            retval[i] = set_breakpoint (lineno, condition);

            if (retval[i] != 0 && ! file.empty ())
              octave_link::update_breakpoint (true, file, retval[i], condition);
          }
      }

    return retval;
  }

  bp_table::intmap
  tree_statement_list::remove_all_breakpoints (const std::string& file)
  {
    bp_table::intmap retval;

    octave_value_list bkpts = list_breakpoints ();

    for (int i = 0; i < bkpts.length (); i++)
      {
        int lineno = static_cast<int> (bkpts(i).int_value ());

        delete_breakpoint (lineno);

        retval[i] = lineno;

        if (! file.empty ())
          octave_link::update_breakpoint (false, file, lineno);
      }

    return retval;
  }

  tree_statement_list *
  tree_statement_list::dup (symbol_table::scope_id scope,
                            symbol_table::context_id context) const
  {
    tree_statement_list *new_list = new tree_statement_list ();

    new_list->function_body = function_body;

    for (const tree_statement *elt : *this)
      new_list->append (elt ? elt->dup (scope, context) : 0);

    return new_list;
  }
}
