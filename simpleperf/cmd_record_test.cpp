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

#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>

#include <map>
#include <memory>

#include "command.h"
#include "environment.h"
#include "event_selection_set.h"
#include "get_test_data.h"
#include "record.h"
#include "record_file.h"
#include "test_util.h"

using namespace PerfFileFormat;

static std::unique_ptr<Command> RecordCmd() {
  return CreateCommandInstance("record");
}

static bool RunRecordCmd(std::vector<std::string> v,
                         const char* output_file = nullptr) {
  std::unique_ptr<TemporaryFile> tmpfile;
  std::string out_file;
  if (output_file != nullptr) {
    out_file = output_file;
  } else {
    tmpfile.reset(new TemporaryFile);
    out_file = tmpfile->path;
  }
  v.insert(v.end(), {"-o", out_file, "sleep", SLEEP_SEC});
  return RecordCmd()->Run(v);
}

TEST(record_cmd, no_options) { ASSERT_TRUE(RunRecordCmd({})); }

TEST(record_cmd, system_wide_option) {
  TEST_IN_ROOT(ASSERT_TRUE(RunRecordCmd({"-a"})));
}

TEST(record_cmd, sample_period_option) {
  ASSERT_TRUE(RunRecordCmd({"-c", "100000"}));
}

TEST(record_cmd, event_option) {
  ASSERT_TRUE(RunRecordCmd({"-e", "cpu-clock"}));
}

TEST(record_cmd, freq_option) {
  ASSERT_TRUE(RunRecordCmd({"-f", "99"}));
  ASSERT_TRUE(RunRecordCmd({"-F", "99"}));
}

TEST(record_cmd, output_file_option) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RecordCmd()->Run({"-o", tmpfile.path, "sleep", SLEEP_SEC}));
}

TEST(record_cmd, dump_kernel_mmap) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RunRecordCmd({}, tmpfile.path));
  std::unique_ptr<RecordFileReader> reader =
      RecordFileReader::CreateInstance(tmpfile.path);
  ASSERT_TRUE(reader != nullptr);
  std::vector<std::unique_ptr<Record>> records = reader->DataSection();
  ASSERT_GT(records.size(), 0U);
  bool have_kernel_mmap = false;
  for (auto& record : records) {
    if (record->type() == PERF_RECORD_MMAP) {
      const MmapRecord* mmap_record =
          static_cast<const MmapRecord*>(record.get());
      if (mmap_record->filename == DEFAULT_KERNEL_MMAP_NAME) {
        have_kernel_mmap = true;
        break;
      }
    }
  }
  ASSERT_TRUE(have_kernel_mmap);
}

TEST(record_cmd, dump_build_id_feature) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RunRecordCmd({}, tmpfile.path));
  std::unique_ptr<RecordFileReader> reader =
      RecordFileReader::CreateInstance(tmpfile.path);
  ASSERT_TRUE(reader != nullptr);
  const FileHeader& file_header = reader->FileHeader();
  ASSERT_TRUE(file_header.features[FEAT_BUILD_ID / 8] &
              (1 << (FEAT_BUILD_ID % 8)));
  ASSERT_GT(reader->FeatureSectionDescriptors().size(), 0u);
}

TEST(record_cmd, tracepoint_event) {
  TEST_IN_ROOT(ASSERT_TRUE(RunRecordCmd({"-a", "-e", "sched:sched_switch"})));
}

TEST(record_cmd, branch_sampling) {
  if (IsBranchSamplingSupported()) {
    ASSERT_TRUE(RunRecordCmd({"-b"}));
    ASSERT_TRUE(RunRecordCmd({"-j", "any,any_call,any_ret,ind_call"}));
    ASSERT_TRUE(RunRecordCmd({"-j", "any,k"}));
    ASSERT_TRUE(RunRecordCmd({"-j", "any,u"}));
    ASSERT_FALSE(RunRecordCmd({"-j", "u"}));
  } else {
    GTEST_LOG_(INFO) << "This test does nothing as branch stack sampling is "
                        "not supported on this device.";
  }
}

TEST(record_cmd, event_modifier) {
  ASSERT_TRUE(RunRecordCmd({"-e", "cpu-cycles:u"}));
}

TEST(record_cmd, fp_callchain_sampling) {
  ASSERT_TRUE(RunRecordCmd({"--call-graph", "fp"}));
}

TEST(record_cmd, system_wide_fp_callchain_sampling) {
  TEST_IN_ROOT(ASSERT_TRUE(RunRecordCmd({"-a", "--call-graph", "fp"})));
}

TEST(record_cmd, dwarf_callchain_sampling) {
  if (IsDwarfCallChainSamplingSupported()) {
    ASSERT_TRUE(RunRecordCmd({"--call-graph", "dwarf"}));
    ASSERT_TRUE(RunRecordCmd({"--call-graph", "dwarf,16384"}));
    ASSERT_FALSE(RunRecordCmd({"--call-graph", "dwarf,65536"}));
    ASSERT_TRUE(RunRecordCmd({"-g"}));
  } else {
    GTEST_LOG_(INFO) << "This test does nothing as dwarf callchain sampling is "
                        "not supported on this device.";
  }
}

TEST(record_cmd, system_wide_dwarf_callchain_sampling) {
  if (IsDwarfCallChainSamplingSupported()) {
    TEST_IN_ROOT(RunRecordCmd({"-a", "--call-graph", "dwarf"}));
  } else {
    GTEST_LOG_(INFO) << "This test does nothing as dwarf callchain sampling is "
                        "not supported on this device.";
  }
}

TEST(record_cmd, no_unwind_option) {
  if (IsDwarfCallChainSamplingSupported()) {
    ASSERT_TRUE(RunRecordCmd({"--call-graph", "dwarf", "--no-unwind"}));
  } else {
    GTEST_LOG_(INFO) << "This test does nothing as dwarf callchain sampling is "
                        "not supported on this device.";
  }
  ASSERT_FALSE(RunRecordCmd({"--no-unwind"}));
}

TEST(record_cmd, post_unwind_option) {
  if (IsDwarfCallChainSamplingSupported()) {
    ASSERT_TRUE(RunRecordCmd({"--call-graph", "dwarf", "--post-unwind"}));
  } else {
    GTEST_LOG_(INFO) << "This test does nothing as dwarf callchain sampling is "
                        "not supported on this device.";
  }
  ASSERT_FALSE(RunRecordCmd({"--post-unwind"}));
  ASSERT_FALSE(
      RunRecordCmd({"--call-graph", "dwarf", "--no-unwind", "--post-unwind"}));
}

TEST(record_cmd, existing_processes) {
  std::vector<std::unique_ptr<Workload>> workloads;
  CreateProcesses(2, &workloads);
  std::string pid_list = android::base::StringPrintf(
      "%d,%d", workloads[0]->GetPid(), workloads[1]->GetPid());
  ASSERT_TRUE(RunRecordCmd({"-p", pid_list}));
}

TEST(record_cmd, existing_threads) {
  std::vector<std::unique_ptr<Workload>> workloads;
  CreateProcesses(2, &workloads);
  // Process id can also be used as thread id in linux.
  std::string tid_list = android::base::StringPrintf(
      "%d,%d", workloads[0]->GetPid(), workloads[1]->GetPid());
  ASSERT_TRUE(RunRecordCmd({"-t", tid_list}));
}

TEST(record_cmd, no_monitored_threads) { ASSERT_FALSE(RecordCmd()->Run({""})); }

TEST(record_cmd, more_than_one_event_types) {
  ASSERT_TRUE(RunRecordCmd({"-e", "cpu-cycles,cpu-clock"}));
  ASSERT_TRUE(RunRecordCmd({"-e", "cpu-cycles", "-e", "cpu-clock"}));
}

TEST(record_cmd, cpu_option) {
  ASSERT_TRUE(RunRecordCmd({"--cpu", "0"}));
  TEST_IN_ROOT(ASSERT_TRUE(RunRecordCmd({"--cpu", "0", "-a"})));
}

TEST(record_cmd, mmap_page_option) {
  ASSERT_TRUE(RunRecordCmd({"-m", "1"}));
  ASSERT_FALSE(RunRecordCmd({"-m", "0"}));
  ASSERT_FALSE(RunRecordCmd({"-m", "7"}));
}

static void CheckKernelSymbol(const std::string& path, bool need_kallsyms,
                              bool* success) {
  *success = false;
  std::unique_ptr<RecordFileReader> reader =
      RecordFileReader::CreateInstance(path);
  ASSERT_TRUE(reader != nullptr);
  std::vector<std::unique_ptr<Record>> records = reader->DataSection();
  bool has_kernel_symbol_records = false;
  for (const auto& record : records) {
    if (record->type() == SIMPLE_PERF_RECORD_KERNEL_SYMBOL) {
      has_kernel_symbol_records = true;
    }
  }
  ASSERT_EQ(need_kallsyms, has_kernel_symbol_records);
  *success = true;
}

TEST(record_cmd, kernel_symbol) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RunRecordCmd({}, tmpfile.path));
  bool success;
  CheckKernelSymbol(tmpfile.path, true, &success);
  ASSERT_TRUE(success);
  ASSERT_TRUE(RunRecordCmd({"--no-dump-kernel-symbols"}, tmpfile.path));
  CheckKernelSymbol(tmpfile.path, false, &success);
  ASSERT_TRUE(success);
}

// Check if the dso/symbol records in perf.data matches our expectation.
static void CheckDsoSymbolRecords(const std::string& path,
                                  bool can_have_dso_symbol_records,
                                  bool* success) {
  *success = false;
  std::unique_ptr<RecordFileReader> reader =
      RecordFileReader::CreateInstance(path);
  ASSERT_TRUE(reader != nullptr);
  std::vector<std::unique_ptr<Record>> records = reader->DataSection();
  bool has_dso_record = false;
  bool has_symbol_record = false;
  std::map<uint64_t, bool> dso_hit_map;
  for (const auto& record : records) {
    if (record->type() == SIMPLE_PERF_RECORD_DSO) {
      has_dso_record = true;
      uint64_t dso_id = static_cast<const DsoRecord*>(record.get())->dso_id;
      ASSERT_EQ(dso_hit_map.end(), dso_hit_map.find(dso_id));
      dso_hit_map.insert(std::make_pair(dso_id, false));
    } else if (record->type() == SIMPLE_PERF_RECORD_SYMBOL) {
      has_symbol_record = true;
      uint64_t dso_id = static_cast<const SymbolRecord*>(record.get())->dso_id;
      auto it = dso_hit_map.find(dso_id);
      ASSERT_NE(dso_hit_map.end(), it);
      it->second = true;
    }
  }
  if (can_have_dso_symbol_records) {
    // It is possible that there are no samples hitting functions having symbol.
    // In that case, there are no dso/symbol records.
    ASSERT_EQ(has_dso_record, has_symbol_record);
    for (auto& pair : dso_hit_map) {
      ASSERT_TRUE(pair.second);
    }
  } else {
    ASSERT_FALSE(has_dso_record);
    ASSERT_FALSE(has_symbol_record);
  }
  *success = true;
}

TEST(record_cmd, dump_symbols) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RunRecordCmd({}, tmpfile.path));
  bool success;
  CheckDsoSymbolRecords(tmpfile.path, false, &success);
  ASSERT_TRUE(success);
  ASSERT_TRUE(RunRecordCmd({"--dump-symbols"}, tmpfile.path));
  CheckDsoSymbolRecords(tmpfile.path, true, &success);
  ASSERT_TRUE(success);
  if (IsDwarfCallChainSamplingSupported()) {
    ASSERT_TRUE(RunRecordCmd({"-g"}, tmpfile.path));
    bool success;
    CheckDsoSymbolRecords(tmpfile.path, false, &success);
    ASSERT_TRUE(success);
    ASSERT_TRUE(RunRecordCmd({"-g", "--dump-symbols"}, tmpfile.path));
    CheckDsoSymbolRecords(tmpfile.path, true, &success);
    ASSERT_TRUE(success);
  }
}

TEST(record_cmd, group_option) {
  ASSERT_TRUE(RunRecordCmd({"--group", "cpu-cycles,cpu-clock", "-m", "16"}));
  ASSERT_TRUE(RunRecordCmd({"--group", "cpu-cycles,cpu-clock", "--group",
                            "cpu-cycles:u,cpu-clock:u", "--group",
                            "cpu-cycles:k,cpu-clock:k", "-m", "16"}));
}

TEST(record_cmd, symfs_option) { ASSERT_TRUE(RunRecordCmd({"--symfs", "/"})); }

TEST(record_cmd, duration_option) {
  TemporaryFile tmpfile;
  ASSERT_TRUE(RecordCmd()->Run({"--duration", "1.2", "-o", tmpfile.path}));
}
