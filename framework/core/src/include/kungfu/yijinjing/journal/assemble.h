// SPDX-License-Identifier: Apache-2.0

//
// Created by Keren Dong on 2020/5/22.
//

#ifndef YIJINJING_ASSEMBLE_H
#define YIJINJING_ASSEMBLE_H

#include <kungfu/yijinjing/journal/journal.h>

namespace kungfu::yijinjing::journal {
class sink {
public:
  sink();
  virtual ~sink() = default;
  virtual void put(const data::location_ptr &location, uint32_t dest_id, const frame_ptr &frame) = 0;
  virtual void close(){};
  [[nodiscard]] publisher_ptr get_publisher();

private:
  publisher_ptr publisher_;
};
DECLARE_PTR(sink)

class null_sink : public sink {
public:
  null_sink() = default;
  void put(const data::location_ptr &location, uint32_t dest_id, const frame_ptr &frame) override{};
};

class copy_sink : public sink {
public:
  explicit copy_sink(data::locator_ptr locator);
  void put(const data::location_ptr &location, uint32_t dest_id, const frame_ptr &frame) override;

private:
  data::locator_ptr locator_;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, writer_ptr>> writers_ = {};
};

class assemble {
public:
  explicit assemble(const std::vector<data::locator_ptr> &locators, const std::string &mode = "*",
                    const std::string &category = "*", const std::string &group = "*", const std::string &name = "*");

  explicit assemble(const std::string &mode = "*", const std::string &category = "*", const std::string &group = "*",
                    const std::string &name = "*");

  explicit assemble(const data::location_ptr &source_location, uint32_t dest_id,
                    uint32_t assemble_mode = longfist::enums::AssembleMode::Channel, int64_t from_time = 0);

  virtual ~assemble() = default;

  assemble operator+(assemble &other);

  void operator>>(const sink_ptr &sink);

  bool data_available();

  void next();

  frame_ptr current_frame();

  template <typename T> std::vector<T> read_all(int32_t msg_type, int64_t end_time = INT64_MAX) {
    std::vector<T> v{};
    while (data_available() and current_frame()->gen_time() < end_time) {
      if (current_frame()->msg_type() == msg_type) {
        v.push_back(current_frame()->template data<T>());
      }
      next();
    }
    return v;
  }

  [[maybe_unused]] void seek_to_time(int64_t nano_time);

  [[nodiscard]] const std::vector<reader_ptr> &get_readers() const { return readers_; }

protected:
  std::vector<reader_ptr> readers_ = {};
  reader_ptr current_reader_ = {};

private:
  const std::string &mode_;
  const std::string &category_;
  const std::string &group_;
  const std::string &name_;
  publisher_ptr publisher_;
  std::vector<data::locator_ptr> locators_ = {};

  void sort();
};
DECLARE_PTR(assemble)
} // namespace kungfu::yijinjing::journal
#endif // YIJINJING_ASSEMBLE_H
