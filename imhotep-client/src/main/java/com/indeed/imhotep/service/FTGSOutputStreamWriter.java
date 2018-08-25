/*
 * Copyright (C) 2018 Indeed Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 */
 package com.indeed.imhotep.service;

import com.indeed.imhotep.FTGSBinaryFormat;
import com.indeed.imhotep.api.FTGSIterator;

import java.io.Closeable;
import java.io.IOException;
import java.io.OutputStream;

public final class FTGSOutputStreamWriter implements Closeable {
    private final OutputStream out;

    private boolean fieldIsIntType;

    private byte[] previousTermBytes = new byte[100];
    private int previousTermLength;
    private byte[] currentTermBytes = new byte[100];
    private int currentTermLength;
    private long previousTermInt;
    private long currentTermInt;
    private boolean closed = false;

    private long currentTermDocFreq;

    private boolean termWritten;
    private boolean fieldWritten = false;

    private int previousGroupId = -1;

    public FTGSOutputStreamWriter(final OutputStream out) {
        this.out = out;
    }

    public void switchField(final String field, final boolean isIntType) throws IOException {
        endField();
        fieldIsIntType = isIntType;
        FTGSBinaryFormat.writeFieldStart(fieldIsIntType, field, out);
        fieldWritten = true;
        previousTermLength = 0;
        previousTermInt = -1;
    }

    public void switchBytesTerm(final byte[] termBytes, final int termLength, final long termDocFreq) throws IOException {
        endTerm();
        currentTermBytes = copyInto(termBytes, termLength, currentTermBytes);
        currentTermLength = termLength;
        currentTermDocFreq = termDocFreq;
    }

    public void switchIntTerm(final long term, final long termDocFreq) throws IOException {
        endTerm();
        currentTermInt = term;
        currentTermDocFreq = termDocFreq;
    }

    public void switchGroup(final int groupId) throws IOException {
        if (!termWritten) {
            writeTerm();
        }
        FTGSBinaryFormat.writeGroup(groupId, previousGroupId, out);
        previousGroupId = groupId;
    }

    private void writeTerm() throws IOException {
        if (fieldIsIntType) {
            FTGSBinaryFormat.writeIntTermStart(currentTermInt, previousTermInt, out);
            previousTermInt = currentTermInt;
        } else {
            FTGSBinaryFormat.writeStringTermStart(currentTermBytes, currentTermLength, previousTermBytes, previousTermLength, out);
            previousTermBytes = copyInto(currentTermBytes, currentTermLength, previousTermBytes);
            previousTermLength = currentTermLength;
        }
        FTGSBinaryFormat.writeTermDocFreq(currentTermDocFreq, out);
        termWritten = true;
    }

    public void addStat(final long stat) throws IOException {
        FTGSBinaryFormat.writeStat(stat, out);
    }

    public void close() throws IOException {
        if (closed) {
            return;
        }
        closed = true;
        endField();
        FTGSBinaryFormat.writeFtgsEndTag(out);
        out.flush();
    }

    private void endField() throws IOException {
        if (!fieldWritten) {
            return;
        }
        endTerm();
        fieldWritten = false;
        FTGSBinaryFormat.writeFieldEnd(fieldIsIntType, out);
    }

    private void endTerm() throws IOException {
        if (termWritten) {
            FTGSBinaryFormat.writeGroupStatsEnd(out);
        }
        termWritten = false;
        previousGroupId = -1;
    }

    public static void write(final FTGSIterator buffer, final OutputStream out) throws IOException {
        final FTGSOutputStreamWriter writer = new FTGSOutputStreamWriter(out);
        writer.write(buffer);
    }

    public void write(final FTGSIterator buffer) throws IOException {
        final long[] stats = new long[buffer.getNumStats()];
        while (buffer.nextField()) {
            final boolean fieldIsIntType = buffer.fieldIsIntType();
            switchField(buffer.fieldName(), fieldIsIntType);
            while (buffer.nextTerm()) {
                if (fieldIsIntType) {
                    switchIntTerm(buffer.termIntVal(), buffer.termDocFreq());
                } else {
                    // termStringBytes() returns a reference so this copies the bytes instead of hanging on to it
                    switchBytesTerm(buffer.termStringBytes(), buffer.termStringLength(), buffer.termDocFreq());
                }
                while (buffer.nextGroup()){
                    switchGroup(buffer.group());
                    buffer.groupStats(stats);
                    for (final long stat : stats) {
                        addStat(stat);
                    }
                }
                endTerm();
            }
        }
        close();
    }

    private static byte[] copyInto(final byte[] src, final int srcLen, byte[] dest) {
        dest = ensureCap(dest, srcLen);
        System.arraycopy(src, 0, dest, 0, srcLen);
        return dest;
    }

    private static byte[] ensureCap(final byte[] b, final int len) {
        if (b == null) {
            return new byte[Math.max(len, 16)];
        }
        if (b.length >= len) {
            return b;
        }
        return new byte[Math.max(b.length*2, len)];
    }

}
