#pragma once

#include "signal_tree_item.h"
#include "text_input.h"
#include "tree_panel.h"
#include "wave_data.h"

namespace sv {

class WaveSignalsPanel : public TreePanel {
 public:
  WaveSignalsPanel();
  void Draw() final;
  void SetScope(const WaveData::SignalScope *s);
  bool Searchable() const final { return true; }
  virtual void UIChar(int ch) final;
  virtual std::string Tooltip() const final;
  std::pair<int, int> ScrollArea() const final;
  std::optional<std::pair<int, int>> CursorLocation() const final;
  void Resized() final;
  bool Modal() const final { return editing_filter_; }

 private:
  const WaveData::SignalScope *scope_;
  std::vector<SignalTreeItem> signals_;

  // Signal type filter toggles.
  bool hide_inputs_ = false;
  bool hide_outputs_ = false;
  bool hide_inouts_ = false;
  bool hide_signals_ = false;

  TextInput filter_input_;
  std::string filter_text_;
  bool editing_filter_ = false;
};

} // namespace sv