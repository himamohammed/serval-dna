package org.servalproject.servaldna.meshmb;

import org.servalproject.servaldna.BundleId;
import org.servalproject.servaldna.SigningKey;
import org.servalproject.servaldna.Subscriber;

/**
 * Created by jeremy on 11/01/17.
 */
public class MeshMBSubscription {
	public final Subscriber subscriber;
	public final String name;
	public final long timestamp;
	public final String lastMessage;

	public MeshMBSubscription(Subscriber subscriber, String name, long timestamp, String lastMessage){
		this.subscriber = subscriber;
		this.name = name;
		this.lastMessage = lastMessage;
		this.timestamp = timestamp;
	}
}