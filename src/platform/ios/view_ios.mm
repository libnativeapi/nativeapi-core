// Views are not available on iOS: IsSupported() is false and every call is a
// no-op, so the interface layer links and the bindings behave predictably.

#include "../../view.h"
#include "../../view_impl.h"
#include "../../window.h"

namespace nativeapi {

struct View::Impl::Platform {};

View::Impl::Impl(View* owner, void* native, bool owned)
    : owner(owner), native(nullptr), owned(owned), id(IdAllocator::Allocate<View>()) {
  (void)native;
}

View::Impl::~Impl() = default;

void* View::Impl::CreateNativeContainer() {
  return nullptr;
}

void View::Impl::SetNativeFrame(Rectangle) {}

Rectangle View::Impl::GetNativeFrame() const {
  return Rectangle{0, 0, 0, 0};
}

Size View::Impl::GetNativeIntrinsicSize() const {
  return Size{0, 0};
}

void View::Impl::AddNativeSubview(Impl&, size_t) {}

void View::Impl::RemoveNativeSubview(Impl&) {}

void View::Impl::SetNativeVisible(bool) {}

void View::Impl::SetNativeEnabled(bool) {}

bool View::Impl::IsNativeEnabled() const {
  return false;
}

void View::Impl::SetNativeBackgroundColor(Color) {}

void View::Impl::SetNativeTooltip(const std::optional<std::string>&) {}

void View::Impl::NativeFocus() {}

void View::Impl::NativeBlur() {}

bool View::Impl::IsNativeFocused() const {
  return false;
}

void View::Impl::StartNativeListening() {}

void View::Impl::StopNativeListening() {}

void View::Impl::ObserveNativeResize(bool) {}

bool View::IsSupported() {
  return false;
}

std::shared_ptr<View> Window::GetContentView() const {
  return nullptr;
}

Label::Label(const std::string&) : View(NativeControl{nullptr}) {}
void Label::SetText(const std::string&) {}
std::string Label::GetText() const {
  return "";
}
void Label::SetTextColor(Color) {}
Color Label::GetTextColor() const {
  return Color{0, 0, 0, 0};
}
void Label::SetFontSize(double) {}
double Label::GetFontSize() const {
  return 0.0;
}
void Label::SetTextAlignment(TextAlignment) {}
TextAlignment Label::GetTextAlignment() const {
  return TextAlignment::Start;
}

Button::Button(const std::string&) : View(NativeControl{nullptr}) {}
void Button::SetText(const std::string&) {}
std::string Button::GetText() const {
  return "";
}

TextField::TextField(const std::string&) : View(NativeControl{nullptr}) {}
void TextField::SetText(const std::string&) {}
std::string TextField::GetText() const {
  return "";
}
void TextField::SetTextColor(Color) {}
Color TextField::GetTextColor() const {
  return Color{0, 0, 0, 0};
}
void TextField::SetFontSize(double) {}
double TextField::GetFontSize() const {
  return 0.0;
}
void TextField::SetTextAlignment(TextAlignment) {}
TextAlignment TextField::GetTextAlignment() const {
  return TextAlignment::Start;
}
void TextField::SetPlaceholder(const std::optional<std::string>&) {}
std::optional<std::string> TextField::GetPlaceholder() const {
  return std::nullopt;
}
void TextField::SetEditable(bool) {}
bool TextField::IsEditable() const {
  return false;
}
void TextField::SetSecure(bool) {}
bool TextField::IsSecure() const {
  return false;
}
void TextField::SetMultiline(bool) {}
bool TextField::IsMultiline() const {
  return false;
}

ImageView::ImageView() : View(NativeControl{nullptr}) {}
void ImageView::SetImage(std::shared_ptr<Image>) {}
std::shared_ptr<Image> ImageView::GetImage() const {
  return nullptr;
}

}  // namespace nativeapi
