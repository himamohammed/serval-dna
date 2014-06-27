/**
 * Copyright (C) 2014 Serval Project Inc.
 *
 * This file is part of Serval Software (http://www.servalproject.org)
 *
 * Serval Software is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this source code; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

package org.servalproject.servaldna.rhizome;

import org.servalproject.json.JSONInputException;
import org.servalproject.json.JSONTokeniser;
import org.servalproject.json.JSONTableScanner;
import org.servalproject.servaldna.ServalDHttpConnectionFactory;
import org.servalproject.servaldna.ServalDInterfaceException;
import org.servalproject.servaldna.BundleId;
import org.servalproject.servaldna.SubscriberId;
import org.servalproject.servaldna.FileHash;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class RhizomeBundleList {

	private ServalDHttpConnectionFactory httpConnector;
	private HttpURLConnection httpConnection;
	private JSONTokeniser json;
	private JSONTableScanner table;
	int rowCount;

	public RhizomeBundleList(ServalDHttpConnectionFactory connector)
	{
		this.httpConnector = connector;
		this.table = new JSONTableScanner()
					.addColumn("_id", Integer.class)
					.addColumn(".token", String.class, JSONTokeniser.Narrow.ALLOW_NULL)
					.addColumn("service", String.class)
					.addColumn("id", BundleId.class)
					.addColumn("version", Long.class)
					.addColumn("date", Long.class)
					.addColumn(".inserttime", Long.class)
					.addColumn(".author", SubscriberId.class, JSONTokeniser.Narrow.ALLOW_NULL)
					.addColumn(".fromhere", Integer.class)
					.addColumn("filesize", Long.class)
					.addColumn("filehash", FileHash.class, JSONTokeniser.Narrow.ALLOW_NULL)
					.addColumn("sender", SubscriberId.class, JSONTokeniser.Narrow.ALLOW_NULL)
					.addColumn("recipient", SubscriberId.class, JSONTokeniser.Narrow.ALLOW_NULL)
					.addColumn("name", String.class);
	}

	public boolean isConnected()
	{
		return this.json != null;
	}

	public void connect() throws IOException, ServalDInterfaceException
	{
		try {
			rowCount = 0;
			httpConnection = httpConnector.newServalDHttpConnection("/restful/rhizome/bundlelist.json");
			httpConnection.connect();
			json = RhizomeCommon.receiveRestfulResponse(httpConnection, HttpURLConnection.HTTP_OK);
			json.consume(JSONTokeniser.Token.START_OBJECT);
			json.consume("header");
			json.consume(JSONTokeniser.Token.COLON);
			table.consumeHeaderArray(json);
			json.consume(JSONTokeniser.Token.COMMA);
			json.consume("rows");
			json.consume(JSONTokeniser.Token.COLON);
			json.consume(JSONTokeniser.Token.START_ARRAY);
		}
		catch (JSONInputException e) {
			throw new ServalDInterfaceException(e);
		}
	}

	public RhizomeBundle nextBundle() throws ServalDInterfaceException, IOException
	{
		try {
			Object tok = json.nextToken();
			if (tok == JSONTokeniser.Token.END_ARRAY) {
				json.consume(JSONTokeniser.Token.END_OBJECT);
				json.consume(JSONTokeniser.Token.EOF);
				return null;
			}
			if (rowCount != 0)
				JSONTokeniser.match(tok, JSONTokeniser.Token.COMMA);
			else
				json.pushToken(tok);
			Map<String,Object> row = table.consumeRowArray(json);
			return new RhizomeBundle(
				rowCount++,
				(int)row.get("_id"),
				(String)row.get(".token"),
				(String)row.get("service"),
				(BundleId)row.get("id"),
				(long)row.get("version"),
				(long)row.get("date"),
				(long)row.get(".inserttime"),
				(SubscriberId)row.get(".author"),
				(int)row.get(".fromhere"),
				(long)row.get("filesize"),
				(FileHash)row.get("filehash"),
				(SubscriberId)row.get("sender"),
				(SubscriberId)row.get("recipient"),
				(String)row.get("name"));
		}
		catch (JSONInputException e) {
			throw new ServalDInterfaceException(e);
		}
	}

	public void close() throws IOException
	{
		httpConnection = null;
		if (json != null) {
			json.close();
			json = null;
		}
	}

}
