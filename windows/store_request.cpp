#include "store_request.h"

#include <algorithm>
#include <utility>

namespace windows_iap {

StoreRequest::StoreRequest(std::unique_ptr<Result> result)
    : result_(std::move(result)) {}

bool StoreRequest::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return result_ != nullptr;
}

void StoreRequest::Cancel(
    const winrt::agile_ref<winrt::Windows::Foundation::IAsyncInfo>&
        operation) noexcept {
  try {
    if (operation) operation.get().Cancel();
  } catch (...) {
    // Store shutdown/cancellation is best effort. Never unwind a destructor or
    // a Windows thread-pool callback if the Store apartment is already gone.
  }
}

void StoreRequest::Watch(winrt::Windows::Foundation::IAsyncInfo operation,
                         std::chrono::milliseconds timeout) {
  if (!operation) {
    throw winrt::hresult_error(
        E_UNEXPECTED, L"Microsoft Store returned no asynchronous operation.");
  }
  auto agile = winrt::make_agile(operation);
  bool active;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active = result_ != nullptr;
    if (active) operation_ = agile;
  }
  if (!active) {
    Cancel(agile);
    return;
  }
  // Interactive Store UI has no artificial deadline: a delayed confirmation
  // must not invite another purchase/review while the first is unresolved.
  if (timeout <= std::chrono::milliseconds::zero()) return;

  std::weak_ptr<StoreRequest> weak = shared_from_this();
  auto timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
      [weak](const auto&) noexcept {
        try {
          if (auto request = weak.lock()) {
            request->Finish(
                [](Result& result) {
                  result.Error("store_timeout",
                               "Microsoft Store query timed out.");
                },
                true);
          }
        } catch (...) {
          // No C++ exception may escape a WinRT timer callback.
        }
      },
      timeout);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active = result_ != nullptr;
    if (active) timer_ = timer;
  }
  if (!active) timer.Cancel();
}

void StoreRequest::Finish(const std::function<void(Result&)>& reply,
                          bool cancel) {
  std::unique_ptr<Result> result;
  winrt::agile_ref<winrt::Windows::Foundation::IAsyncInfo> operation;
  winrt::Windows::System::Threading::ThreadPoolTimer timer{nullptr};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_) return;
    result = std::move(result_);
    operation = std::move(operation_);
    timer = std::move(timer_);
  }
  // Cancel can synchronously complete an operation; release the mutex first.
  if (timer) {
    try {
      timer.Cancel();
    } catch (...) {
      // The reply and Store cancellation must still happen if the timer closed.
    }
  }
  try {
    reply(*result);
  } catch (...) {
    if (cancel) Cancel(operation);
    throw;
  }
  if (cancel) Cancel(operation);
}

void StoreRequest::SuccessInternal(const flutter::EncodableValue* value) {
  Finish(
      [value](Result& result) {
        if (value)
          result.Success(*value);
        else
          result.Success();
      },
      false);
}

void StoreRequest::ErrorInternal(const std::string& code,
                                 const std::string& message,
                                 const flutter::EncodableValue* details) {
  Finish(
      [&](Result& result) {
        if (details)
          result.Error(code, message, *details);
        else
          result.Error(code, message);
      },
      true);
}

void StoreRequest::NotImplementedInternal() {
  Finish([](Result& result) { result.NotImplemented(); }, true);
}

void StoreRequest::Shutdown() {
  Error("plugin_disposed", "Microsoft Store plugin is shutting down.");
}

std::shared_ptr<StoreRequest> StoreRequestManager::Add(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                 [](const auto& request) {
                                   const auto active = request.lock();
                                   return !active || !active->pending();
                                 }),
                  requests_.end());
  auto request = std::make_shared<StoreRequest>(std::move(result));
  requests_.push_back(request);
  return request;
}

void StoreRequestManager::Shutdown() {
  auto requests = std::move(requests_);
  for (const auto& weak : requests) {
    if (auto request = weak.lock()) {
      try {
        request->Shutdown();
      } catch (...) {
        // One failed reply must not prevent cancellation of other requests.
      }
    }
  }
}

}  // namespace windows_iap
