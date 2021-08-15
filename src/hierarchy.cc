#include "hierarchy.h"
#include "absl/strings/str_format.h"
#include "color.h"
#include "utils.h"
#include "workspace.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <uhdm/headers/gen_scope.h>
#include <uhdm/headers/gen_scope_array.h>
#include <uhdm/headers/module.h>
#include <uhdm/headers/task_func.h>

namespace sv {
namespace {

// Limit number of instances dumped into the UI at once.
constexpr int kMaxExpandInstances = 100;

std::vector<const UHDM::any *> get_subs(const UHDM::any *item) {
  std::vector<const UHDM::any *> subs;
  if (item->VpiType() == vpiModule) {
    auto m = dynamic_cast<const UHDM::module *>(item);
    if (m->Modules() != nullptr) {
      subs.insert(subs.end(), m->Modules()->cbegin(), m->Modules()->cend());
    }
    if (m->Gen_scope_arrays() != nullptr) {
      subs.insert(subs.end(), m->Gen_scope_arrays()->cbegin(),
                  m->Gen_scope_arrays()->cend());
    }
    if (m->Task_funcs() != nullptr) {
      subs.insert(subs.end(), m->Task_funcs()->cbegin(),
                  m->Task_funcs()->cend());
    }
    // TODO: Other stuff ?
  } else if (item->VpiType() == vpiGenScopeArray) {
    // TODO: What to do if there is 0 or 2+ GenScopes in here??
    auto ga = dynamic_cast<const UHDM::gen_scope_array *>(item);
    auto g = (*ga->Gen_scopes())[0];
    if (g->Modules() != nullptr) {
      subs.insert(subs.end(), g->Modules()->cbegin(), g->Modules()->cend());
    }
    if (g->Gen_scope_arrays() != nullptr) {
      subs.insert(subs.end(), g->Gen_scope_arrays()->cbegin(),
                  g->Gen_scope_arrays()->cend());
    }
  }
  // TODO: Also offer lexical sort?
  std::stable_sort(subs.begin(), subs.end(),
                   [](const UHDM::any *a, const UHDM::any *b) {
                     return a->VpiLineNo() < b->VpiLineNo();
                   });

  return subs;
}

bool is_expandable(const UHDM::any *item) {
  // TODO: This could be optimized without actually creating the array, but
  // would be lots of duplicated code.
  return !get_subs(item).empty();
}

} // namespace

Hierarchy::Hierarchy(int h, int w, int row, int col) : Panel(h, w, row, col) {
  // Populate an intial list of instances, top modules really.
  for (auto &top : *Workspace::Get().Design()->TopModules()) {
    entries_.push_back(top);
    entry_info_[top].expandable = is_expandable(top);
  }
  // Put all top modules with sub instances on top.
  // Lexical sort within that.
  auto top_sorter = [](const UHDM::any *a, const UHDM::any *b) {
    auto ma = dynamic_cast<const UHDM::module *>(a);
    auto mb = dynamic_cast<const UHDM::module *>(b);
    bool a_has_subs = ma->Modules() != nullptr || ma->Gen_scope_arrays();
    bool b_has_subs = mb->Modules() != nullptr || mb->Gen_scope_arrays();
    if (a_has_subs == b_has_subs) {
      return a->VpiName() < b->VpiName();
    } else {
      return a_has_subs;
    }
  };
  std::stable_sort(entries_.begin(), entries_.end(), top_sorter);

  // First instance is pre-expanded for usability if there is just one.
  if (entries_.size() == 1) ToggleExpand();
}

void Hierarchy::Draw() {
  werase(w_);
  int win_w, win_h;
  getmaxyx(w_, win_h, win_w);
  // If the window was resized and the ui line position is now hidden, move it
  // up.
  int max_string_len = 0;
  for (int y = 0; y < win_h; ++y) {
    const int entry_idx = y + scroll_row_;
    if (entry_idx >= entries_.size()) break;
    if (entry_idx == line_idx_ && !search_preview_) {
      wattron(w_, has_focus_ ? A_REVERSE : A_UNDERLINE);
    }
    auto entry = entries_[entry_idx];
    auto info = entry_info_[entry];
    std::string indent(info.depth, ' ');
    if (info.more_idx != 0) {
      SetColor(w_, kHierShowMorePair);
      mvwprintw(w_, y, 0, "%s...more...", indent.c_str());
    } else {
      std::string def_name = StripWorklib(entry->VpiDefName());
      if (entry->VpiType() == vpiGenScopeArray) {
        def_name = "[generate]";
      } else if (entry->VpiType() == vpiFunction) {
        def_name = "[function]";
      } else if (entry->VpiType() == vpiTask) {
        def_name = "[task]";
      } else if (entry->VpiType() != vpiModule) {
        def_name = absl::StrFormat("[%d]", entry->VpiType());
      }
      // VpiName generally doesn't contain the worklib, but it does for the top
      // level modules.
      auto inst_name = StripWorklib(entry->VpiName());
      char exp = info.expandable ? (info.expanded ? '-' : '+') : ' ';
      std::string s = indent + exp + inst_name + ' ' + def_name;
      max_string_len = std::max(max_string_len, static_cast<int>(s.size()));
      int expand_pos = indent.size();
      int inst_pos = expand_pos + 1;
      int type_pos = inst_pos + inst_name.size() + 1;
      const bool show_search =
          search_preview_ && entry_idx == line_idx_ && search_start_col_ >= 0;
      const int search_pos = search_start_col_ + inst_pos;
      for (int j = 0; j < s.size(); ++j) {
        const int x = j - ui_col_scroll_;
        if (x < 0) continue;
        if (x >= win_w) break;
        if (x == 0 && ui_col_scroll_ != 0 && j >= expand_pos) {
          // Show an overflow character on the left edge if the ui has been
          // scrolled horizontally.
          SetColor(w_, kOverflowTextPair);
          mvwaddch(w_, y, x, '<');
        } else if (x == win_w - 1 && j < s.size() - 1) {
          // Replace the last character with an overflow indicator if the line
          // extends beyond the window width.
          SetColor(w_, kOverflowTextPair);
          mvwaddch(w_, y, x, '>');
        } else {
          if (j >= type_pos) {
            SetColor(w_, (entry->VpiType() != vpiModule) ? kHierOtherPair
                                                         : kHierModulePair);
          } else if (j >= inst_pos) {
            if (show_search && j == search_pos) {
              wattron(w_, A_REVERSE);
            }
            SetColor(w_, kHierInstancePair);
          } else if (j == expand_pos && info.expandable) {
            SetColor(w_, kHierExpandPair);
          }
          mvwaddch(w_, y, x, s[j]);
          if (show_search && j == search_pos + search_text_.size() - 1) {
            wattroff(w_, A_REVERSE);
          }
        }
      }
    }
    wattrset(w_, A_NORMAL);
  }
  ui_max_col_scroll_ = max_string_len - win_w;
}

void Hierarchy::ToggleExpand() {
  const auto entry_it = entries_.cbegin() + line_idx_;
  if (entries_.empty()) return;
  auto &info = entry_info_[*entry_it];
  if (info.more_idx != 0) {
    // Check if the "... more ..." entry needs to be further expanded.
    int stopped_pos = info.more_idx;
    info.more_idx = 0;
    std::vector<const UHDM::any *> new_entries;
    const auto parent_subs = get_subs(info.parent);
    for (int i = stopped_pos + 1; i < parent_subs.size(); ++i) {
      auto new_sub = parent_subs[i];
      int more_idx = (i - stopped_pos) >= kMaxExpandInstances ? i : 0;
      new_entries.push_back(new_sub);
      entry_info_[new_sub] = {.depth = info.depth,
                              .expandable = is_expandable(new_sub),
                              .more_idx = more_idx,
                              .parent = info.parent};
      if (more_idx != 0) break;
    }
    entries_.insert(entry_it + 1, new_entries.cbegin(), new_entries.cend());
    return;
  } else if (!info.expandable) {
    return;
  } else if (info.expanded) {
    // Delete everything under the current index that has greater depth.
    auto last = entry_it + 1;
    while (last != entries_.cend() && entry_info_[*last].depth > info.depth) {
      last++;
    }
    entries_.erase(entry_it + 1, last);
    info.expanded = false;
  } else {
    std::vector<const UHDM::any *> new_entries;
    std::function<void(const UHDM::any *, std::vector<const UHDM::any *> &)>
        recurse_add_subs =
            [&](const UHDM::any *item, std::vector<const UHDM::any *> &list) {
              int sub_idx = 0;
              auto subs = get_subs(item);
              for (auto &sub : subs) {
                list.push_back(sub);
                // Create new UI info only if it doesn't already exist
                if (entry_info_.find(sub) == entry_info_.end()) {
                  int more_idx = sub_idx >= kMaxExpandInstances ? sub_idx : 0;
                  entry_info_[sub] = {.depth = entry_info_[item].depth + 1,
                                      .expandable = is_expandable(sub),
                                      .more_idx = more_idx,
                                      .parent = item};
                  sub_idx++;
                  if (more_idx != 0) break;
                } else if (entry_info_[sub].more_idx != 0) {
                  break;
                } else if (entry_info_[sub].expanded) {
                  recurse_add_subs(sub, list);
                }
              }
            };
    recurse_add_subs(*entry_it, new_entries);
    entries_.insert(entry_it + 1, new_entries.cbegin(), new_entries.cend());
    info.expanded = true;
    // Move the selection up a ways if the new lines are all hidden.
    const int win_h = getmaxy(w_);
    if (line_idx_ - scroll_row_ == win_h - 1) {
      const int lines_below = entries_.size() - scroll_row_ - win_h;
      const int scroll_amt = std::min(lines_below, win_h / 3);
      scroll_row_ += scroll_amt;
    }
  }
}

void Hierarchy::SetItem(const UHDM::any *item) {
  // First, build a list of all things up to the root.
  std::vector<const UHDM::any *> path;
  while (item != nullptr) {
    // Skip GenScope, here we only consider GenScopeArray
    if (item->VpiType() != vpiGenScope) {
      path.push_back(item);
    }
    item = item->VpiParent();
  }
  // Now look through the list at every level and expand as necessary.
  // Iterate backwards so that the root is the first thing looked for.
  for (int path_idx = path.size() - 1; path_idx >= 0; --path_idx) {
    auto &item = path[path_idx];
    for (int entry_idx = 0; entry_idx < entries_.size(); ++entry_idx) {
      auto &entry = entries_[entry_idx];
      if (item == entry) {
        line_idx_ = entry_idx;
        // Expand the item if it isn't already, and if it isn't the last one.
        if (entry_info_[entry].expandable && !entry_info_[entry].expanded &&
            path_idx != 0) {
          ToggleExpand();
        }
        break;
      }
    }
  }
  SetLineAndScroll(line_idx_);
}

void Hierarchy::UIChar(int ch) {
  switch (ch) {
  case 'i': load_instance_ = true; break;
  case 'd': load_definition_ = true; break;
  case 'u': {
    int current_depth = entry_info_[entries_[line_idx_]].depth;
    if (current_depth != 0 && line_idx_ != 0) {
      while (entry_info_[entries_[line_idx_]].depth >= current_depth) {
        line_idx_--;
      }
      if (line_idx_ - scroll_row_ < 0) {
        scroll_row_ = line_idx_;
      }
    }
    break;
  }
  case 0x20: // space
  case 0xd:  // enter
    ToggleExpand();
    break;
  case 'h':
  case 0x104: // left
    if (ui_col_scroll_ > 0) ui_col_scroll_--;
    break;
  case 'l':
  case 0x105: // right
    if (ui_col_scroll_ < ui_max_col_scroll_) ui_col_scroll_++;
    break;
  case 'k':
  default: Panel::UIChar(ch);
  }
}

bool Hierarchy::Search(bool search_down) {
  if (entries_.size() < 2) return false;
  int idx = line_idx_;
  const int start_idx = idx;
  const auto step = [&] {
    idx += search_down ? 1 : -1;
    if (idx < 0) {
      idx = entries_.size() - 1;
    } else if (idx >= entries_.size()) {
      idx = 0;
    }
  };
  while (1) {
    if (!search_preview_) step();
    const auto pos =
        StripWorklib(entries_[idx]->VpiName()).find(search_text_, 0);
    if (pos != std::string::npos) {
      search_start_col_ = pos;
      SetLineAndScroll(idx);
      return true;
    }
    if (search_preview_) step();
    if (idx == start_idx) {
      search_start_col_ = -1;
      return false;
    }
  }
}

std::optional<std::pair<const UHDM::any *, bool>> Hierarchy::ItemForSource() {
  if (load_definition_ || load_instance_) {
    std::pair<const UHDM::any *, bool> ret = {entries_[line_idx_],
                                              load_definition_};
    load_definition_ = false;
    load_instance_ = false;
    return ret;
  }
  return std::nullopt;
}

std::string Hierarchy::Tooltip() const {
  return "i:instance  d:definition  u:up scope";
}

} // namespace sv
