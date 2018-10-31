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

package com.indeed.imhotep.fs.sql;

import org.apache.commons.io.IOUtils;
import org.apache.log4j.Logger;
import org.jooq.impl.TableImpl;

import javax.sql.DataSource;
import java.io.IOException;
import java.io.InputStream;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;

/**
 * @author kenh
 */

public class SchemaInitializer {
    private static final Logger LOGGER = Logger.getLogger(SchemaInitializer.class);
    private final DataSource dataSource;

    public SchemaInitializer(final DataSource dataSource) {
        this.dataSource = dataSource;
    }

    private void executeSql(final String schemaFile) throws SQLException, IOException {
        try (Connection connection = dataSource.getConnection();
             final Statement statement = connection.createStatement();
             final InputStream inputStream = getClass().getClassLoader().getResourceAsStream(schemaFile)
        ) {
            final String contents = IOUtils.toString(inputStream);
            statement.execute(contents);
        }
    }

    public void initialize(final List<? extends TableImpl> tables) throws IOException, SQLException {
        for (final TableImpl table : tables) {
            final String schemaFile = "schema/" + table.getName() + ".sql";
            LOGGER.info("Initializing schema in " + schemaFile);
            executeSql(schemaFile);
        }
    }
}