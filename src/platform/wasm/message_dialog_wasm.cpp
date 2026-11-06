#include "../../message_dialog.h"
#include "../message_dialog_state.h"
namespace nativeapi {
class MessageDialog::Impl {
 public:
  MessageDialogState state_;
  std::string title;
  std::string message;
  void RefreshExtended() {}
};
MessageDialog::MessageDialog(const std::string& title, const std::string& message)
    : pimpl_(std::make_unique<Impl>()), modality_(DialogModality::None) {
  pimpl_->title = title;
  pimpl_->message = message;
}
MessageDialog::~MessageDialog() = default;
void MessageDialog::SetTitle(const std::string& title) {
  pimpl_->title = title;
}
std::string MessageDialog::GetTitle() const {
  return pimpl_->title;
}
void MessageDialog::SetMessage(const std::string& message) {
  pimpl_->message = message;
}
std::string MessageDialog::GetMessage() const {
  return pimpl_->message;
}
DialogModality MessageDialog::GetModality() const {
  return modality_;
}
void MessageDialog::SetModality(DialogModality modality) {
  modality_ = modality;
}
bool MessageDialog::Open() {
  return false;
}
bool MessageDialog::Close() {
  return false;
}
bool MessageDialog::IsExtendedSupported() {
  return false;
}
#include "../message_dialog_extensions.inc"
}  // namespace nativeapi
