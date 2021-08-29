#pragma once

#include "tree_panel.h"
#include "text_input.h"
#include "wave_data.h"

namespace sv {

// The constructor pulls data from the Workspace wave data. Bad things happen if
// there is none.
class WavesPanel : public TreePanel {
 public:
  WavesPanel();
  void Draw() final;
  void UIChar(int ch) final;
  std::string Tooltip() const final;
  void Resized() final;
  std::optional<std::pair<int, int>> CursorLocation() const final;
  bool Modal() const final { return inputting_time_; }

 private:
  double TimePerChar() const;
  void CycleTimeUnits();
  int cursor_pos_ = 0;
  uint64_t cursor_time_ = 0;
  uint64_t marker_time_ = 0;
  uint64_t numbered_marker_times_[10];
  uint64_t left_time_ = 0;
  uint64_t right_time_ = 0;
  bool marker_selection_ = false;
  TextInput time_input_;
  bool inputting_time_ = false;
  int time_unit_ = -9; // nanoseconds.

  // Charachters reserved for the signal name and value.
  int name_size_ = 15;
  int value_size_ = 12;
  // Convenience to avoid repeated workspace Get() calls.
  const WaveData *wave_data_;
};

} // namespace sv
