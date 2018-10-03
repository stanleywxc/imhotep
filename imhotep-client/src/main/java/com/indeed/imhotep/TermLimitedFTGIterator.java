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

package com.indeed.imhotep;

import com.indeed.imhotep.api.FTGIterator;


/**
 * Wrapper for an FTGSIterator that will only return up to 'termLimit' terms that have at least 1 group.
 * Terms that don't have at least 1 non-0 group are not counted.
 * @author vladimir
 */

public class TermLimitedFTGIterator<T extends FTGIterator> implements FTGIterator {
    protected final T wrapped;
    private final long termLimit;
    private long termsIterated = 0;
    private boolean firstTermGroupConsumed = false;
    private long termDocFreq = 0;

    /**
     * @param wrapped The iterator to use
     * @param termLimit Maximum number of terms that will be allowed to iterate through
     */
    public TermLimitedFTGIterator(final T wrapped, final long termLimit) {
        this.wrapped = wrapped;
        this.termLimit = termLimit > 0 ? termLimit : Long.MAX_VALUE;
    }

    @Override
    public int getNumGroups() {
        return wrapped.getNumGroups();
    }

    @Override
    public boolean nextField() {
        return wrapped.nextField();
    }

    @Override
    public String fieldName() {
        return wrapped.fieldName();
    }

    @Override
    public boolean fieldIsIntType() {
        return wrapped.fieldIsIntType();
    }

    @Override
    public boolean nextTerm() {
        if (termsIterated >= termLimit) {
            return false;
        }
        final boolean hasNext = wrapped.nextTerm();
        if (hasNext) {
            termDocFreq = wrapped.termDocFreq();
            firstTermGroupConsumed = nextGroup();
            if(firstTermGroupConsumed) {
                termsIterated++;
            }
        }
        return hasNext;
    }

    @Override
    public long termDocFreq() {
        return termDocFreq;
    }

    @Override
    public long termIntVal() {
        return wrapped.termIntVal();
    }

    @Override
    public String termStringVal() {
        return wrapped.termStringVal();
    }

    @Override
    public byte[] termStringBytes() {
        return wrapped.termStringBytes();
    }

    @Override
    public int termStringLength() {
        return wrapped.termStringLength();
    }

    @Override
    public boolean nextGroup() {
        if(firstTermGroupConsumed) {
            firstTermGroupConsumed = false;
            return true;
        }
        return wrapped.nextGroup();
    }

    @Override
    public int group() {
        return wrapped.group();
    }

    @Override
    public void close() {
        wrapped.close();
    }
}