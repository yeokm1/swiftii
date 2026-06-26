/* Unit-test runner: a tiny harness so each `*_test.c` can stay focused
 * on its assertions. Each test function returns 0 for pass, nonzero
 * for fail. We aggregate the results and exit nonzero if any failed.
 *
 * Adding a test:
 *   1. Write `int test_my_thing(void)` in some `tests/unit/foo_test.c`.
 *   2. Declare it `extern` in this file.
 *   3. Add a `RUN(test_my_thing)` line below.
 */

#include <stdio.h>
#include <string.h>

extern int test_vm_hello(void);

extern int test_heap_alloc_payload(void);
extern int test_heap_multiple_allocs_distinct(void);
extern int test_heap_topmost_release_reclaims(void);
extern int test_heap_mid_release_leaks(void);
extern int test_heap_retain_release_balanced(void);
extern int test_heap_oom_returns_null(void);
extern int test_heap_reset_clears(void);

extern int test_lex_keywords_and_idents(void);
extern int test_lex_integers(void);
extern int test_lex_int_overflow(void);
extern int test_lex_long_ident_accepted(void);
extern int test_lex_operators(void);
extern int test_lex_comments_and_newlines(void);
extern int test_lex_string_simple(void);
extern int test_lex_string_escapes(void);
extern int test_lex_string_bad_escape(void);
extern int test_lex_string_interp_bounds(void);
extern int test_lex_string_unterm_interp(void);

extern int test_osdetect_caps_default_is_safe(void);
extern int test_osdetect_init_populates_machine_type(void);
extern int test_osdetect_struct_size_is_packed(void);
extern int test_osdetect_machine_id_classification(void);

extern int test_input_untranslate_roundtrip(void);
extern int test_input_auto_lowercase(void);
extern int test_input_single_case_marker(void);
extern int test_input_run_case_marker(void);
extern int test_input_ctrl_w_underscore(void);
extern int test_input_digraphs(void);
extern int test_input_string_apostrophe_contextual(void);
extern int test_input_string_digraphs_translate(void);
extern int test_input_string_escape_passthrough(void);
extern int test_input_comments(void);
extern int test_input_empty_and_partial(void);
extern int test_input_idempotent(void);

extern int test_compile_demo_program(void);
extern int test_compile_arithmetic(void);
extern int test_compile_comparison(void);
extern int test_compile_logical_ops(void);
extern int test_compile_var_reassignment(void);
extern int test_compile_let_is_immutable(void);
extern int test_compile_undeclared_var_is_error(void);
extern int test_compile_ident_too_long_is_error(void);
extern int test_compile_func_name_too_long_is_error(void);
extern int test_compile_ident_at_limit_ok(void);
extern int test_compile_semicolons(void);
extern int test_compile_negative_literal(void);
extern int test_compile_string_literal(void);
extern int test_compile_string_escapes(void);
extern int test_compile_string_concat(void);
extern int test_compile_string_concat_var(void);
extern int test_compile_string_interp_int(void);
extern int test_compile_string_interp_expr(void);
extern int test_compile_string_interp_bool(void);
extern int test_compile_if_then(void);
extern int test_compile_if_false_skips(void);
extern int test_compile_if_else(void);
extern int test_compile_if_else_if_chain(void);
extern int test_compile_while(void);
extern int test_compile_while_false_never_runs(void);
extern int test_compile_nested_if(void);
extern int test_compile_nested_loops(void);
extern int test_compile_for_half_open_range(void);
extern int test_compile_for_closed_range(void);
extern int test_compile_for_empty_range(void);
extern int test_compile_fizzbuzz(void);

extern int test_compile_func_void(void);
extern int test_compile_func_one_arg_underscore(void);
extern int test_compile_func_labeled_args(void);
extern int test_compile_func_local_let(void);
extern int test_compile_func_recursion(void);
extern int test_compile_func_recursion_overflow(void);
extern int test_compile_func_nested_call(void);
extern int test_compile_func_return_value_required(void);
extern int test_compile_func_wrong_arity(void);
extern int test_compile_return_outside_function(void);
extern int test_compile_func_local_shadows_global(void);

extern int test_compile_break_in_while(void);
extern int test_compile_break_in_for_in(void);
extern int test_compile_break_outside_loop_is_error(void);
extern int test_compile_multiple_breaks_in_one_loop(void);
extern int test_compile_print_terminator_empty(void);
extern int test_compile_print_terminator_space(void);
extern int test_compile_print_terminator_only(void);
extern int test_compile_print_terminator_expr(void);
extern int test_compile_readline_eof(void);

extern int test_compile_force_unwrap_some(void);
extern int test_compile_nil_coalesce_takes_default(void);
extern int test_compile_nil_coalesce_takes_value(void);
extern int test_compile_if_let_some(void);
extern int test_compile_if_let_nil_skips(void);
extern int test_compile_if_let_else(void);
extern int test_compile_if_let_inside_function(void);

extern int test_compile_array_literal_and_count(void);
extern int test_compile_array_empty_literal(void);
extern int test_compile_array_append_with_capture(void);
extern int test_compile_array_methods(void);
extern int test_compile_array_remove_last_empty_runtime_error(void);
extern int test_compile_array_subscript_oob_runtime_error(void);
extern int test_compile_func_arg_type_check_ok(void);
extern int test_compile_func_arg_type_mismatch(void);
extern int test_compile_min_max(void);
extern int test_compile_min_wrong_type(void);
extern int test_compile_string_of_int(void);
extern int test_compile_string_of_int_wrong_type(void);
extern int test_compile_asc(void);
extern int test_compile_asc_wrong_type(void);
extern int test_compile_chr(void);
extern int test_compile_chr_wrong_type(void);
extern int test_compile_int_from_string(void);
extern int test_compile_int_from_string_wrong_type(void);
extern int test_compile_peek_poke(void);
extern int test_compile_home(void);
extern int test_compile_peek_wrong_type(void);
extern int test_compile_poke_wrong_type(void);
extern int test_compile_htab_vtab(void);
extern int test_compile_htab_wrong_type(void);
extern int test_compile_vtab_range_runtime(void);
extern int test_compile_gr(void);
extern int test_compile_text80(void);
extern int test_compile_text80_wrong_arity(void);
extern int test_compile_color_wrong_type(void);
extern int test_compile_color_range_runtime(void);
extern int test_compile_plot_range_runtime(void);
extern int test_compile_gr_full(void);
extern int test_compile_plot_full_oob_runtime(void);
extern int test_compile_plot_mixed_y_runtime(void);
extern int test_compile_hlin_vlin_scrn(void);
extern int test_compile_hlin_wrong_arity(void);
extern int test_compile_hlin_range_runtime(void);
extern int test_compile_array_is_empty(void);
extern int test_compile_array_subscript_set(void);
extern int test_compile_array_subscript_set_let_is_const(void);
extern int test_compile_array_subscript_set_type_mismatch(void);
extern int test_compile_array_subscript_set_oob_runtime_error(void);
extern int test_compile_array_in_function(void);

extern int test_gapbuf_init_empty(void);
extern int test_gapbuf_insert_basic(void);
extern int test_gapbuf_insert_full(void);
extern int test_gapbuf_delete_left(void);
extern int test_gapbuf_delete_right(void);
extern int test_gapbuf_insert_middle(void);
extern int test_gapbuf_move_clamps(void);
extern int test_gapbuf_serialize_bounds(void);
extern int test_gapbuf_load(void);
extern int test_gapbuf_load_too_big(void);
extern int test_gapbuf_at_out_of_range(void);
extern int test_gapbuf_round_trip(void);

extern int test_textnav_line_start_end(void);
extern int test_textnav_col_and_index(void);
extern int test_textnav_line_count_and_at(void);
extern int test_textnav_up_down_keeps_column(void);
extern int test_textnav_up_down_edges(void);
extern int test_textnav_empty_buffer(void);

extern int test_screen_glyph_width(void);
extern int test_screen_gutter_width(void);
extern int test_screen_basic_render(void);
extern int test_screen_wrap(void);
extern int test_screen_scroll_wrap(void);
extern int test_screen_cursor_column(void);
extern int test_screen_digraph_width(void);
extern int test_screen_overflow_marker(void);
extern int test_screen_vertical_scroll(void);
extern int test_screen_status_and_message(void);
extern int test_screen_status_cursor_column(void);
extern int test_screen_empty_buffer(void);

extern int test_keymap_insert_and_dirty(void);
extern int test_keymap_backspace(void);
extern int test_keymap_return_inserts_newline(void);
extern int test_keymap_cursor_moves(void);
extern int test_keymap_up_down(void);
extern int test_keymap_arrows(void);
extern int test_keymap_ctrl_d_backspace(void);
extern int test_keymap_io_actions(void);
extern int test_keymap_ignores_other_controls(void);
extern int test_keymap_page_up_down(void);
extern int test_keymap_cook_key(void);
extern int test_keymap_scroll_follows(void);

extern int test_fileio_save_canonicalizes(void);
extern int test_fileio_load_verbatim(void);
extern int test_fileio_round_trip(void);
extern int test_fileio_text_verbatim(void);
extern int test_fileio_text_load_verbatim(void);
extern int test_fileio_raw_swift_verbatim(void);
extern int test_fileio_path_is_swift(void);
extern int test_fileio_load_notfound(void);

extern int test_session_type_save_load_render(void);
extern int test_session_iiplus_cooked_keystrokes(void);
extern int test_session_edit_save_run(void);
extern int test_session_backspace_correction_run(void);

extern int test_histring_older_basic(void);
extern int test_histring_newer_walks_back(void);
extern int test_histring_parks_in_progress(void);
extern int test_histring_park_only_once(void);
extern int test_histring_add_rejects(void);
extern int test_histring_wraps_and_evicts(void);

extern int test_userfile_write_then_read(void);
extern int test_userfile_read_missing(void);
extern int test_userfile_empty(void);
extern int test_compile_read_file(void);
extern int test_compile_write_file(void);
extern int test_compile_write_file_wrong_arity(void);
extern int test_userfile_append_creates_and_grows(void);
extern int test_pf_delete_and_exists(void);
extern int test_pf_rename(void);
extern int test_pf_mkdir_and_list(void);
extern int test_userdir_open_missing(void);
extern int test_compile_list_directory_type(void);
extern int test_compile_delete_directory_alias(void);
extern int test_run_write_then_read_roundtrip(void);
extern int test_run_append_and_delete(void);
extern int test_swb_stream_roundtrip(void);
extern int test_swb_open_image_matches_read(void);
extern int test_swb_roundtrip_arithmetic(void);
extern int test_swb_roundtrip_string_const(void);
extern int test_swb_roundtrip_func_call(void);
extern int test_swb_roundtrip_mixed(void);
extern int test_swb_image_has_header(void);
extern int test_swb_bad_magic(void);
extern int test_swb_bad_version(void);
extern int test_swb_truncated_header(void);
extern int test_swb_truncated_body(void);
extern int test_swb_bc_cap(void);
extern int test_swb_roundtrip_extras_builtins(void);
extern int test_swb_bad_program_start(void);
extern int test_swb_bad_func_start(void);
extern int test_swb_out_full(void);
extern int test_srcwin_stream_matches_whole(void);
extern int test_srcwin_small_file(void);
extern int test_srcwin_statement_too_long(void);

#define RUN(fn) \
  do { \
    int rc = fn(); \
    ++total; \
    if (rc != 0) { \
      ++failed; \
      printf("FAIL %s (rc=%d)\n", #fn, rc); \
    } else { \
      printf("ok   %s\n", #fn); \
    } \
  } while (0)

int main(void) {
  int total = 0;
  int failed = 0;

  RUN(test_vm_hello);

  RUN(test_heap_alloc_payload);
  RUN(test_heap_multiple_allocs_distinct);
  RUN(test_heap_topmost_release_reclaims);
  RUN(test_heap_mid_release_leaks);
  RUN(test_heap_retain_release_balanced);
  RUN(test_heap_oom_returns_null);
  RUN(test_heap_reset_clears);

  RUN(test_lex_keywords_and_idents);
  RUN(test_lex_integers);
  RUN(test_lex_int_overflow);
  RUN(test_lex_long_ident_accepted);
  RUN(test_lex_operators);
  RUN(test_lex_comments_and_newlines);
  RUN(test_lex_string_simple);
  RUN(test_lex_string_escapes);
  RUN(test_lex_string_bad_escape);
  RUN(test_lex_string_interp_bounds);
  RUN(test_lex_string_unterm_interp);

  RUN(test_osdetect_caps_default_is_safe);
  RUN(test_osdetect_init_populates_machine_type);
  RUN(test_osdetect_struct_size_is_packed);
  RUN(test_osdetect_machine_id_classification);

  RUN(test_input_untranslate_roundtrip);
  RUN(test_input_auto_lowercase);
  RUN(test_input_single_case_marker);
  RUN(test_input_run_case_marker);
  RUN(test_input_ctrl_w_underscore);
  RUN(test_input_digraphs);
  RUN(test_input_string_apostrophe_contextual);
  RUN(test_input_string_digraphs_translate);
  RUN(test_input_string_escape_passthrough);
  RUN(test_input_comments);
  RUN(test_input_empty_and_partial);
  RUN(test_input_idempotent);

  extern int test_error_paths_compile(void);
  extern int test_error_paths_limits(void);
  extern int test_error_paths_runtime(void);
  RUN(test_error_paths_compile);
  RUN(test_error_paths_limits);
  RUN(test_error_paths_runtime);
  RUN(test_compile_demo_program);
  RUN(test_compile_arithmetic);
  RUN(test_compile_comparison);
  RUN(test_compile_logical_ops);
  RUN(test_compile_var_reassignment);
  RUN(test_compile_let_is_immutable);
  RUN(test_compile_undeclared_var_is_error);
  RUN(test_compile_ident_too_long_is_error);
  RUN(test_compile_func_name_too_long_is_error);
  RUN(test_compile_ident_at_limit_ok);
  RUN(test_compile_semicolons);
  RUN(test_compile_negative_literal);
  RUN(test_compile_string_literal);
  RUN(test_compile_string_escapes);
  RUN(test_compile_string_concat);
  RUN(test_compile_string_concat_var);
  RUN(test_compile_string_interp_int);
  RUN(test_compile_string_interp_expr);
  RUN(test_compile_string_interp_bool);
  RUN(test_compile_if_then);
  RUN(test_compile_if_false_skips);
  RUN(test_compile_if_else);
  RUN(test_compile_if_else_if_chain);
  RUN(test_compile_while);
  RUN(test_compile_while_false_never_runs);
  RUN(test_compile_nested_if);
  RUN(test_compile_nested_loops);
  RUN(test_compile_for_half_open_range);
  RUN(test_compile_for_closed_range);
  RUN(test_compile_for_empty_range);
  RUN(test_compile_fizzbuzz);

  RUN(test_compile_func_void);
  RUN(test_compile_func_one_arg_underscore);
  RUN(test_compile_func_labeled_args);
  RUN(test_compile_func_local_let);
  RUN(test_compile_func_recursion);
  RUN(test_compile_func_recursion_overflow);
  RUN(test_compile_func_nested_call);
  RUN(test_compile_func_return_value_required);
  RUN(test_compile_func_wrong_arity);
  RUN(test_compile_return_outside_function);
  RUN(test_compile_func_local_shadows_global);

  RUN(test_compile_break_in_while);
  RUN(test_compile_break_in_for_in);
  RUN(test_compile_break_outside_loop_is_error);
  RUN(test_compile_multiple_breaks_in_one_loop);
  RUN(test_compile_print_terminator_empty);
  RUN(test_compile_print_terminator_space);
  RUN(test_compile_print_terminator_only);
  RUN(test_compile_print_terminator_expr);
  RUN(test_compile_readline_eof);

  RUN(test_compile_force_unwrap_some);
  RUN(test_compile_nil_coalesce_takes_default);
  RUN(test_compile_nil_coalesce_takes_value);
  RUN(test_compile_if_let_some);
  RUN(test_compile_if_let_nil_skips);
  RUN(test_compile_if_let_else);
  RUN(test_compile_if_let_inside_function);

  RUN(test_compile_array_literal_and_count);
  RUN(test_compile_array_empty_literal);
  RUN(test_compile_array_append_with_capture);
  RUN(test_compile_array_methods);
  RUN(test_compile_array_remove_last_empty_runtime_error);
  RUN(test_compile_array_subscript_oob_runtime_error);
  RUN(test_compile_func_arg_type_check_ok);
  RUN(test_compile_func_arg_type_mismatch);
  RUN(test_compile_min_max);
  RUN(test_compile_min_wrong_type);
  RUN(test_compile_string_of_int);
  RUN(test_compile_string_of_int_wrong_type);
  RUN(test_compile_asc);
  RUN(test_compile_asc_wrong_type);
  RUN(test_compile_chr);
  RUN(test_compile_chr_wrong_type);
  RUN(test_compile_int_from_string);
  RUN(test_compile_int_from_string_wrong_type);
  RUN(test_compile_peek_poke);
  RUN(test_compile_home);
  RUN(test_compile_peek_wrong_type);
  RUN(test_compile_poke_wrong_type);
  RUN(test_compile_htab_vtab);
  RUN(test_compile_htab_wrong_type);
  RUN(test_compile_vtab_range_runtime);
  RUN(test_compile_gr);
  RUN(test_compile_text80);
  RUN(test_compile_text80_wrong_arity);
  RUN(test_compile_color_wrong_type);
  RUN(test_compile_color_range_runtime);
  RUN(test_compile_plot_range_runtime);
  RUN(test_compile_gr_full);
  RUN(test_compile_plot_full_oob_runtime);
  RUN(test_compile_plot_mixed_y_runtime);
  RUN(test_compile_hlin_vlin_scrn);
  RUN(test_compile_hlin_wrong_arity);
  RUN(test_compile_hlin_range_runtime);
  RUN(test_compile_array_is_empty);
  RUN(test_compile_array_subscript_set);
  RUN(test_compile_array_subscript_set_let_is_const);
  RUN(test_compile_array_subscript_set_type_mismatch);
  RUN(test_compile_array_subscript_set_oob_runtime_error);
  RUN(test_compile_array_in_function);

  RUN(test_gapbuf_init_empty);
  RUN(test_gapbuf_insert_basic);
  RUN(test_gapbuf_insert_full);
  RUN(test_gapbuf_delete_left);
  RUN(test_gapbuf_delete_right);
  RUN(test_gapbuf_insert_middle);
  RUN(test_gapbuf_move_clamps);
  RUN(test_gapbuf_serialize_bounds);
  RUN(test_gapbuf_load);
  RUN(test_gapbuf_load_too_big);
  RUN(test_gapbuf_at_out_of_range);
  RUN(test_gapbuf_round_trip);

  RUN(test_textnav_line_start_end);
  RUN(test_textnav_col_and_index);
  RUN(test_textnav_line_count_and_at);
  RUN(test_textnav_up_down_keeps_column);
  RUN(test_textnav_up_down_edges);
  RUN(test_textnav_empty_buffer);

  RUN(test_screen_glyph_width);
  RUN(test_screen_gutter_width);
  RUN(test_screen_basic_render);
  RUN(test_screen_wrap);
  RUN(test_screen_scroll_wrap);
  RUN(test_screen_cursor_column);
  RUN(test_screen_digraph_width);
  RUN(test_screen_overflow_marker);
  RUN(test_screen_vertical_scroll);
  RUN(test_screen_status_and_message);
  RUN(test_screen_status_cursor_column);
  RUN(test_screen_empty_buffer);

  RUN(test_keymap_insert_and_dirty);
  RUN(test_keymap_backspace);
  RUN(test_keymap_return_inserts_newline);
  RUN(test_keymap_cursor_moves);
  RUN(test_keymap_up_down);
  RUN(test_keymap_arrows);
  RUN(test_keymap_ctrl_d_backspace);
  RUN(test_keymap_io_actions);
  RUN(test_keymap_ignores_other_controls);
  RUN(test_keymap_page_up_down);
  RUN(test_keymap_cook_key);
  RUN(test_keymap_scroll_follows);

  RUN(test_fileio_save_canonicalizes);
  RUN(test_fileio_load_verbatim);
  RUN(test_fileio_round_trip);
  RUN(test_fileio_text_verbatim);
  RUN(test_fileio_text_load_verbatim);
  RUN(test_fileio_raw_swift_verbatim);
  RUN(test_fileio_path_is_swift);
  RUN(test_fileio_load_notfound);

  RUN(test_session_type_save_load_render);
  RUN(test_session_iiplus_cooked_keystrokes);
  RUN(test_session_edit_save_run);
  RUN(test_session_backspace_correction_run);

  RUN(test_histring_older_basic);
  RUN(test_histring_newer_walks_back);
  RUN(test_histring_parks_in_progress);
  RUN(test_histring_park_only_once);
  RUN(test_histring_add_rejects);
  RUN(test_histring_wraps_and_evicts);

  RUN(test_userfile_write_then_read);
  RUN(test_userfile_read_missing);
  RUN(test_userfile_empty);
  RUN(test_compile_read_file);
  RUN(test_compile_write_file);
  RUN(test_compile_write_file_wrong_arity);
  RUN(test_userfile_append_creates_and_grows);
  RUN(test_pf_delete_and_exists);
  RUN(test_pf_rename);
  RUN(test_pf_mkdir_and_list);
  RUN(test_userdir_open_missing);
  RUN(test_compile_list_directory_type);
  RUN(test_compile_delete_directory_alias);
  RUN(test_run_write_then_read_roundtrip);
  RUN(test_run_append_and_delete);
  RUN(test_swb_stream_roundtrip);
  RUN(test_swb_open_image_matches_read);
  RUN(test_swb_roundtrip_arithmetic);
  RUN(test_swb_roundtrip_string_const);
  RUN(test_swb_roundtrip_func_call);
  RUN(test_swb_roundtrip_mixed);
  RUN(test_swb_image_has_header);
  RUN(test_swb_bad_magic);
  RUN(test_swb_bad_version);
  RUN(test_swb_truncated_header);
  RUN(test_swb_truncated_body);
  RUN(test_swb_bc_cap);
  RUN(test_swb_roundtrip_extras_builtins);
  RUN(test_swb_bad_program_start);
  RUN(test_swb_bad_func_start);
  RUN(test_swb_out_full);

  RUN(test_srcwin_stream_matches_whole);
  RUN(test_srcwin_small_file);
  RUN(test_srcwin_statement_too_long);

  printf("--- %d test(s), %d failed\n", total, failed);
  return failed == 0 ? 0 : 1;
}
