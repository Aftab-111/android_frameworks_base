/*
 * Copyright (C) 2017 The Android Open Source Project
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
#define LOG_TAG "incident_helper"

#include <android/util/ProtoOutputStream.h>

#include "frameworks/base/core/proto/android/os/ps.proto.h"
#include "ih_util.h"
#include "PsParser.h"

using namespace android::os;

status_t PsParser::Parse(const int in, const int out) const {
    Reader reader(in);
    string line;
    header_t header;  // the header of /d/wakeup_sources
    vector<int> columnIndices; // task table can't be split by purely delimiter, needs column positions.
    record_t record;  // retain each record
    int nline = 0;
    int diff = 0;

    ProtoOutputStream proto;
    Table table(PsDumpProto::Process::_FIELD_NAMES, PsDumpProto::Process::_FIELD_IDS, PsDumpProto::Process::_FIELD_COUNT);
    const char* pcyNames[] = { "fg", "bg", "ta" };
    const int pcyValues[] = {PsDumpProto::Process::POLICY_FG, PsDumpProto::Process::POLICY_BG, PsDumpProto::Process::POLICY_TA};
    table.addEnumTypeMap("pcy", pcyNames, pcyValues, 3);
    const char* sNames[] = { "D", "R", "S", "T", "t", "X", "Z" };
    const int sValues[] = {PsDumpProto::Process::STATE_D, PsDumpProto::Process::STATE_R, PsDumpProto::Process::STATE_S, PsDumpProto::Process::STATE_T, PsDumpProto::Process::STATE_TRACING, PsDumpProto::Process::STATE_X, PsDumpProto::Process::STATE_Z};
    table.addEnumTypeMap("s", sNames, sValues, 7);

    // Parse line by line
    while (reader.readLine(&line)) {
        if (line.empty()) continue;

        if (nline++ == 0) {
            header = parseHeader(line, DEFAULT_WHITESPACE);

            const char* headerNames[] = { "LABEL", "USER", "PID", "TID", "PPID", "VSZ", "RSS", "WCHAN", "ADDR", "S", "PRI", "NI", "RTPRIO", "SCH", "PCY", "TIME", "CMD", NULL };
            if (!getColumnIndices(columnIndices, headerNames, line)) {
                return -1;
            }

            continue;
        }

        record = parseRecordByColumns(line, columnIndices);

        diff = record.size() - header.size();
        if (diff < 0) {
            // TODO: log this to incident report!
            fprintf(stderr, "[%s]Line %d has %d missing fields\n%s\n", this->name.string(), nline, -diff, line.c_str());
            printRecord(record);
            continue;
        } else if (diff > 0) {
            // TODO: log this to incident report!
            fprintf(stderr, "[%s]Line %d has %d extra fields\n%s\n", this->name.string(), nline, diff, line.c_str());
            printRecord(record);
            continue;
        }

        long long token = proto.start(PsDumpProto::PROCESSES);
        for (int i=0; i<(int)record.size(); i++) {
            if (!table.insertField(&proto, header[i], record[i])) {
                fprintf(stderr, "[%s]Line %d has bad value %s of %s\n",
                        this->name.string(), nline, header[i].c_str(), record[i].c_str());
            }
        }
        proto.end(token);
    }

    if (!reader.ok(&line)) {
        fprintf(stderr, "Bad read from fd %d: %s\n", in, line.c_str());
        return -1;
    }

    if (!proto.flush(out)) {
        fprintf(stderr, "[%s]Error writing proto back\n", this->name.string());
        return -1;
    }
    fprintf(stderr, "[%s]Proto size: %zu bytes\n", this->name.string(), proto.size());
    return NO_ERROR;
}
