#include "hierarchy.h"
#include "Design/ModuleInstance.h"
#include "SourceCompile/VObjectTypes.h"
#include "absl/strings/str_format.h"
#include "color.h"

namespace sv {
namespace {

// Limit number of instances dumped into the UI at once.
constexpr int kMaxExpandInstances = 500;

bool is_interesting(SURELOG::ModuleInstance *inst) {
  // No need to clutter the hierarchy with entries for:
  // - sequential blocks
  // These things can be inspected in the source code viewer.
  return inst->getType() != slSeq_block;
}

std::string strip_worklib(const std::string &s) {
  const int lib_delimieter_pos = s.find('@');
  if (lib_delimieter_pos == std::string::npos) return s;
  return s.substr(lib_delimieter_pos + 1);
}

bool has_sub_instances(SURELOG::ModuleInstance *inst) {
  bool ret = false;
  for (auto &sub : inst->getAllSubInstances()) {
    if (is_interesting(sub)) return true;
  }
  return ret;
}
} // namespace

Hierarchy::Hierarchy(WINDOW *w) : Panel(w) {}

void Hierarchy::Draw() {
  werase(w_);
  int win_w, win_h;
  getmaxyx(w_, win_h, win_w);
  // If the window was resized and the ui line position is now hidden, move it
  // up.
  if (ui_line_index_ >= win_h) {
    const int shift = ui_line_index_ - (win_h - 1);
    ui_line_index_ -= shift;
    ui_row_scroll_ += shift;
  }
  int max_string_len = 0;
  for (int y = 0; y < win_h; ++y) {
    const int instance_idx = y + ui_row_scroll_;
    if (instance_idx >= instances_.size()) break;
    if (y == ui_line_index_) wattron(w_, A_REVERSE);
    auto line = instances_[instance_idx];
    std::string indent(line.depth, ' ');
    if (line.more_idx != 0) {
      wcolor_set(w_, kHierShowMore, nullptr);
      mvwprintw(w_, y, 0, "%s...more...", indent.c_str());
    } else {
      auto type = line.instance->getType();
      std::string type_name;
      const bool type_is_generate =
          type == slGenerate_block || type == slConditional_generate_construct;
      if (type_is_generate) {
        type_name = "[generate]";
      } else if (type == slModule_instantiation ||
                 type == slModule_declaration) {
        // Module declarations in the hierarchy show up as root nodes, i.e.
        // individual tops.
        type_name = strip_worklib(line.instance->getModuleName());
      } else {
        // For unknown stuff, just print the module name and the Surelog TypeID.
        // TODO: Remove once all supported things are handled.
        type_name = strip_worklib(line.instance->getModuleName()) + ' ' +
                    absl::StrFormat("(type = %d)", type);
      }
      auto inst_name = strip_worklib(line.instance->getInstanceName());
      char exp = line.expandable ? (line.expanded ? '-' : '+') : ' ';
      std::string s = indent + exp + inst_name + ' ' + type_name;
      max_string_len = std::max(max_string_len, static_cast<int>(s.size()));
      // printf("DBG:%d - %s\n\r", instance_idx, s.c_str());
      // mvwprintw(w_, y, 0, "%d - %s", instance_idx, s.c_str());
      int expand_pos = indent.size();
      int inst_pos = expand_pos + 1;
      int type_pos = inst_pos + inst_name.size() + 1;
      for (int j = 0; j < s.size(); ++j) {
        const int x = j - ui_col_scroll_;
        if (x < 0) continue;
        if (x >= win_w) break;
        if (x == 0 && ui_col_scroll_ != 0 && j >= expand_pos) {
          // Show an overflow character on the left edge if the ui has been
          // scrolled horizontally.
          wcolor_set(w_, kOverflowTextPair, nullptr);
          mvwaddch(w_, y, x, '<');
        } else if (x == win_w - 1 && j < s.size() - 1) {
          // Replace the last character with an overflow indicator if the line
          // extends beyond the window width.
          wcolor_set(w_, kOverflowTextPair, nullptr);
          mvwaddch(w_, y, x, '>');
        } else {
          if (j >= type_pos) {
            wcolor_set(w_, type_is_generate ? kHierGenerate : kHierModulePair,
                       nullptr);
          } else if (j >= inst_pos) {
            wcolor_set(w_, kHierInstancePair, nullptr);
          } else if (j == expand_pos && line.expandable) {
            wcolor_set(w_, kHierExpandPair, nullptr);
          }
          mvwaddch(w_, y, x, s[j]);
        }
      }
    }
    wattrset(w_, A_NORMAL);
  }
  ui_max_col_scroll_ = max_string_len - win_w;
}

void Hierarchy::ExpandAt(int idx) {
  auto inst_it = instances_.cbegin() + idx;
  int sub_idx = 0;
  std::vector<InstanceLine> new_lines;
  for (auto &sub : inst_it->instance->getAllSubInstances()) {
    if (!is_interesting(sub)) continue;
    int more_idx = sub_idx > kMaxExpandInstances ? sub_idx : 0;
    new_lines.push_back({.instance = sub,
                         .depth = inst_it->depth + 1,
                         .expandable = has_sub_instances(sub),
                         .more_idx = more_idx});
    sub_idx++;
    if (more_idx != 0) break;
  }
  instances_.insert(inst_it + 1, new_lines.cbegin(), new_lines.cend());
  // Move the selection up a ways if the new lines are all hidden.
  const int win_h = getmaxy(w_);
  if (ui_line_index_ == win_h - 1) {
    const int lines_below = instances_.size() - ui_row_scroll_ - win_h;
    const int scroll_amt = std::min(lines_below, win_h / 3);
    ui_row_scroll_ += scroll_amt;
    ui_line_index_ -= scroll_amt;
  }
}

void Hierarchy::ToggleExpand() {
  const int idx = ui_line_index_ + ui_row_scroll_;
  const auto inst_it = instances_.cbegin() + idx;
  if (inst_it->more_idx != 0) {
    instances_[idx].more_idx = 0;
    std::vector<InstanceLine> new_lines;
    auto parent_subs = inst_it->instance->getParent()->getAllSubInstances();
    for (int i = inst_it->more_idx + 1; i < parent_subs.size(); ++i) {
      if (!is_interesting(parent_subs[i])) continue;
      int more_idx = (i - inst_it->more_idx) > kMaxExpandInstances ? i : 0;
      new_lines.push_back({.instance = parent_subs[i],
                           .depth = inst_it->depth,
                           .expandable = has_sub_instances(parent_subs[i]),
                           .more_idx = more_idx});
      if (more_idx != 0) break;
    }
    instances_.insert(inst_it + 1, new_lines.cbegin(), new_lines.cend());
    return;
  } else if (!inst_it->expandable) {
    return;
  } else if (inst_it->expanded) {
    // Delete everything under the current index that has greater depth.
    auto last = inst_it + 1;
    while (last != instances_.cend() && last->depth > inst_it->depth) {
      last++;
    }
    instances_.erase(inst_it + 1, last);
  } else {
    ExpandAt(idx);
  }
  instances_[idx].expanded = !instances_[idx].expanded;
}

void Hierarchy::UIChar(int ch) {
  const int data_idx = ui_line_index_ + ui_row_scroll_;
  int win_h = getmaxy(w_);
  switch (ch) {
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
  case 0x103: // up
    if (ui_line_index_ == 0 && ui_row_scroll_ != 0) {
      ui_row_scroll_--;
    } else if (ui_line_index_ > 0) {
      ui_line_index_--;
    }
    break;
  case 'j':
  case 0x102: // down
    if (data_idx < instances_.size() - 1) {
      if (ui_line_index_ < win_h - 1) {
        ui_line_index_++;
      } else {
        ui_row_scroll_++;
      }
    }
    break;
  case 0x153: { // PgUp
    int step = std::min(ui_row_scroll_, win_h - 2);
    ui_row_scroll_ -= step;
    ui_line_index_ = std::min(win_h - 2, ui_line_index_ + step);
    break;
  }
  case 0x152: { // PgDn
    int step =
        std::min(static_cast<int>(instances_.size()) - (ui_row_scroll_ + win_h),
                 win_h - 2);
    ui_row_scroll_ += step;
    if (step == 0) {
      ui_line_index_ = win_h - 1;
    } else {
      ui_line_index_ = std::max(2, ui_line_index_ - step);
    }
    break;
  }
  case 'g':   // vim style
  case 0x217: // Ctrl Home
    ui_row_scroll_ = 0;
    ui_line_index_ = 0;
    break;
  case 'G':   // vim style
  case 0x212: // Ctrl End
    if (instances_.size() > win_h) {
      ui_row_scroll_ = instances_.size() - win_h;
      ui_line_index_ = win_h - 1;
    } else {
      ui_line_index_ = instances_.size();
    }
    break;
  }
}

bool Hierarchy::TransferPending() {
  if (load_source_) {
    load_source_ = false;
    return true;
  }
  return false;
}

SURELOG::ModuleInstance *Hierarchy::InstanceForSource() {
  return instances_[ui_line_index_ + ui_row_scroll_].instance;
}

void Hierarchy::SetDesign(SURELOG::Design *d) {
  design_ = d;
  for (auto &top : d->getTopLevelModuleInstances()) {
    instances_.push_back(
        {.instance = top, .depth = 0, .expandable = has_sub_instances(top)});
  }
  ui_line_index_ = 0;
  ui_row_scroll_ = 0;
  // First instance is pre-expanded for usability if there is just one.
  if (instances_.size() == 1) ToggleExpand();
}

} // namespace sv