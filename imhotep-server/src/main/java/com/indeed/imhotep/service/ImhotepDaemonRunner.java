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

import com.indeed.flamdex.api.FlamdexReader;
import com.indeed.flamdex.reader.MockFlamdexReader;
import com.indeed.imhotep.client.Host;

import java.io.IOException;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.nio.file.Path;
import java.util.concurrent.TimeoutException;

/**
 * @author jsgroth
 */
public class ImhotepDaemonRunner {
    private final Path rootShardsDir;
    private final Path tempDir;
    private final int port;
    private int actualPort;
    private final FlamdexReaderSource flamdexFactory;

    private LocalImhotepServiceConfig config = new LocalImhotepServiceConfig();
    private long memoryCapacity = 1024L * 1024 * 1024 * 1024;
    private ImhotepDaemon currentlyRunning;

    public ImhotepDaemonRunner(final Path rootShardsDir, final Path tempDir, final int port) {
        this(rootShardsDir, tempDir, port, new FlamdexReaderSource() {
            @Override
            public FlamdexReader openReader(final Path directory) throws IOException {
                return new MockFlamdexReader();
            }

            @Override
            public FlamdexReader openReader(Path directory, int numDocs) throws IOException {
                return openReader(directory);
            }

        });
    }

    public ImhotepDaemonRunner(
            final Path rootShardsDir,
            final Path tempDir,
            final int port,
            final FlamdexReaderSource flamdexFactory) {
        this.rootShardsDir = rootShardsDir;
        this.tempDir = tempDir;
        this.port = port;
        this.flamdexFactory = flamdexFactory;        
    }

    public void setConfig(final LocalImhotepServiceConfig config) {
        this.config = config;
    }

    public void setMemoryCapacity(final long memoryCapacity) {
        this.memoryCapacity = memoryCapacity;
    }

    public int getPort() {
        return port;
    }

    public int getActualPort() {
        return actualPort;
    }

    public void start() throws IOException, TimeoutException {
        if (currentlyRunning != null) {
            currentlyRunning.shutdown(false);
        }

        String myHostName = config.getAdvertisedHostName();
        if (myHostName == null) {
            myHostName = InetAddress.getLocalHost().getCanonicalHostName();
        }
        currentlyRunning =
                new ImhotepDaemon(new ServerSocket(port),
                                  new LocalImhotepServiceCore(tempDir,
                                                              memoryCapacity,
                                                              flamdexFactory,
                                          config,
                                          rootShardsDir,
                                          new Host(myHostName, port)),
                                  null, null, "localhost", port, null);
        actualPort = currentlyRunning.getPort();

        new Thread(new Runnable() {
            @Override
            public void run() {
                currentlyRunning.run();
            }
        }).start();
        currentlyRunning.waitForStartup(10000L);
    }

    public void stop() throws IOException {
        if (currentlyRunning != null) {
            currentlyRunning.shutdown(false);
            currentlyRunning = null;
        }
    }
}
