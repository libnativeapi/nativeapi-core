// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../file_dialog_impl.h"
namespace nativeapi {
bool FileDialog::IsSupported() {
  return false;
}
FileDialog::Impl::~Impl() = default;
bool FileDialog::Impl::Open() {
  error = "File dialogs are not implemented on this platform";
  result = FileDialogResult::Failed;
  return false;
}
bool FileDialog::Impl::Close() {
  return false;
}
}  // namespace nativeapi
