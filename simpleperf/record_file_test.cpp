/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "environment.h"
#include "event_attr.h"
#include "event_fd.h"
#include "event_type.h"
#include "record.h"
#include "record_file.h"

using namespace PerfFileFormat;

class RecordFileTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    filename = "temporary.record_file";
    const EventType* event_type = EventTypeFactory::FindEventTypeByName("cpu-cycles");
    event_attr = CreateDefaultPerfEventAttr(*event_type);
    std::unique_ptr<EventFd> event_fd = EventFd::OpenEventFileForProcess(event_attr, getpid());
    ASSERT_TRUE(event_fd != nullptr);
    event_fds.push_back(std::move(event_fd));
  }

  std::string filename;
  perf_event_attr event_attr;
  std::vector<std::unique_ptr<EventFd>> event_fds;
};

TEST_F(RecordFileTest, smoke) {
  // Write to a record file.
  std::unique_ptr<RecordFileWriter> writer =
      RecordFileWriter::CreateInstance(filename, event_attr, event_fds);
  ASSERT_TRUE(writer != nullptr);

  // Write Data section.
  MmapRecord mmap_record;
  mmap_record.header.type = PERF_RECORD_MMAP;
  mmap_record.header.size = sizeof(mmap_record);
  ASSERT_TRUE(writer->WriteData(mmap_record.BinaryFormat()));
  ASSERT_TRUE(writer->Close());

  // Read from a record file.
  std::unique_ptr<RecordFileReader> reader = RecordFileReader::CreateInstance(filename);
  ASSERT_TRUE(reader != nullptr);
  const FileHeader* file_header = reader->FileHeader();
  ASSERT_TRUE(file_header != nullptr);
  std::vector<const FileAttr*> attrs = reader->AttrSection();
  ASSERT_EQ(1u, attrs.size());
  ASSERT_EQ(0, memcmp(&attrs[0]->attr, &event_attr, sizeof(perf_event_attr)));
  std::vector<uint64_t> ids = reader->IdsForAttr(attrs[0]);
  ASSERT_EQ(1u, ids.size());

  // Read and check data section.
  std::vector<std::unique_ptr<const Record>> records = reader->DataSection();
  ASSERT_EQ(1u, records.size());
  ASSERT_EQ(mmap_record.header.type, records[0]->header.type);

  ASSERT_TRUE(reader->Close());
}