package com.indeed.imhotep.commands;

import com.indeed.imhotep.GroupStatsIteratorCombiner;
import com.indeed.imhotep.ImhotepRemoteSession;
import com.indeed.imhotep.api.CommandSerializationParameters;
import com.indeed.imhotep.api.GroupStatsIterator;
import com.indeed.imhotep.api.ImhotepOutOfMemoryException;
import com.indeed.imhotep.api.ImhotepSession;
import com.indeed.imhotep.io.ImhotepProtobufShipping;
import com.indeed.imhotep.io.RequestTools.ImhotepRequestSender;
import com.indeed.imhotep.protobuf.DocStat;
import com.indeed.imhotep.protobuf.ImhotepRequest;
import com.indeed.imhotep.protobuf.ImhotepResponse;
import com.indeed.imhotep.utils.tempfiles.ImhotepTempFiles;
import lombok.EqualsAndHashCode;
import lombok.ToString;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

@EqualsAndHashCode(callSuper = true)
@ToString
public class GetGroupStats extends AbstractImhotepCommand<GroupStatsIterator> {

    private final String groupsName;
    private final List<String> stats;

    public GetGroupStats(final String groupsName, final List<String> stats, final String sessionId) {
        super(sessionId, GroupStatsIterator.class);
        this.groupsName = groupsName;
        this.stats = stats;
    }

    @Override
    public ImhotepRequestSender imhotepRequestSenderInitializer() {
        final ImhotepRequest request = ImhotepRequest.newBuilder().setRequestType(ImhotepRequest.RequestType.STREAMING_GET_GROUP_STATS)
                .setInputGroups(groupsName)
                .setSessionId(getSessionId())
                .addDocStat(DocStat.newBuilder().addAllStat(stats))
                .setHasStats(true)
                .build();

        return ImhotepRequestSender.Cached.create(request);
    }

    @Override
    public GroupStatsIterator combine(final List<GroupStatsIterator> subResults) {
        return new GroupStatsIteratorCombiner(subResults.toArray(new GroupStatsIterator[0]));
    }

    @Override
    public GroupStatsIterator apply(final ImhotepSession session) throws ImhotepOutOfMemoryException {
        return session.getGroupStatsIterator(groupsName, stats);
    }

    @Override
    public GroupStatsIterator readResponse(final InputStream is, final CommandSerializationParameters serializationParameters) throws IOException, ImhotepOutOfMemoryException {
        final ImhotepResponse response = ImhotepRemoteSession.readResponseWithMemoryExceptionSessionId(is, serializationParameters.getHost(), serializationParameters.getPort(), getSessionId());
        final BufferedInputStream tempFileStream = ImhotepRemoteSession.saveResponseToFileFromStream(is, ImhotepTempFiles.Type.BATCH_GROUP_STATS_ITERATOR, serializationParameters.getTempFileSizeBytesLeft(), getSessionId());
        final GroupStatsIterator groupStatsIterator = ImhotepProtobufShipping.readGroupStatsIterator(
                tempFileStream, response.getGroupStatSize(), false
        );
        return groupStatsIterator;
    }

}
