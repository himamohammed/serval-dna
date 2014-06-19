/*
Serval DNA MeshMS HTTP RESTful interface
Copyright (C) 2013,2014 Serval Project Inc.
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "conf.h"
#include "serval.h"
#include "httpd.h"
#include "strbuf_helpers.h"

static void finalise_union_meshms_conversationlist(httpd_request *r)
{
  meshms_free_conversations(r->u.mclist.conv);
  r->u.mclist.conv = NULL;
}

static void finalise_union_meshms_messagelist(httpd_request *r)
{
  meshms_message_iterator_close(&r->u.msglist.iter);
}

static void finalise_union_meshms_sendmessage(httpd_request *r)
{
  form_buf_malloc_release(&r->u.sendmsg.message);
}

#define MESHMS_TOKEN_STRLEN (BASE64_ENCODED_LEN(sizeof(rhizome_bid_t) + sizeof(uint64_t)))
#define alloca_meshms_token(bid, offset) meshms_token_to_str(alloca(MESHMS_TOKEN_STRLEN + 1), (bid), (offset))

static char *meshms_token_to_str(char *buf, const rhizome_bid_t *bid, uint64_t offset)
{
  struct iovec iov[2];
  iov[0].iov_base = (void *) bid->binary;
  iov[0].iov_len = sizeof bid->binary;
  iov[1].iov_base = &offset;
  iov[1].iov_len = sizeof offset;
  size_t n = base64url_encodev(buf, iov, 2);
  assert(n == MESHMS_TOKEN_STRLEN);
  buf[n] = '\0';
  return buf;
}

static int strn_to_meshms_token(const char *str, rhizome_bid_t *bidp, uint64_t *offsetp, const char **afterp)
{
  unsigned char token[sizeof bidp->binary + sizeof *offsetp];
  if (base64url_decode(token, sizeof token, str, 0, afterp, 0, NULL) != sizeof token)
    return 0;
  memcpy(bidp->binary, token, sizeof bidp->binary);
  memcpy(offsetp, token + sizeof bidp->binary, sizeof *offsetp);
  return 1;
}

static int http_request_meshms_response(struct httpd_request *r, uint16_t result, const char *message, enum meshms_status status)
{
  r->http.response.result_extra_label = "meshms_status_code";
  r->http.response.result_extra_value.type = JSON_INTEGER;
  r->http.response.result_extra_value.u.integer = status;
  switch (status) {
    case MESHMS_STATUS_OK:
      if (!result)
	result = 200;
      if (!message)
	message = "OK";
      break;
    case MESHMS_STATUS_UPDATED:
      if (!result)
	result = 201;
      if (!message)
	message = "Updated";
      break;
    case MESHMS_STATUS_SID_LOCKED:
      if (!result)
	result = 403;
      if (!message)
	message = "Identity unknown";
      break;
    case MESHMS_STATUS_PROTOCOL_FAULT:
      if (!result)
	result = 403;
      if (!message)
	message = "MeshMS protocol fault";
      break;
    case MESHMS_STATUS_ERROR:
      if (!result)
	result = 500;
      break;
    default:
      WHYF("Invalid MeshMS status code %d", status);
      result = 500;
      break;
  }
  assert(result != 0);
  http_request_simple_response(&r->http, result, message);
  return result;
}

static HTTP_HANDLER restful_meshms_conversationlist_json;
static HTTP_HANDLER restful_meshms_messagelist_json;
static HTTP_HANDLER restful_meshms_newsince_messagelist_json;
static HTTP_HANDLER restful_meshms_sendmessage;

int restful_meshms_(httpd_request *r, const char *remainder)
{
  r->http.response.header.content_type = CONTENT_TYPE_JSON;
  if (!is_rhizome_http_enabled())
    return 403;
  const char *verb = HTTP_VERB_GET;
  HTTP_HANDLER *handler = NULL;
  const char *end;
  if (strn_to_sid_t(&r->sid1, remainder, SIZE_MAX, &end) != -1) {
    remainder = end;
    if (strcmp(remainder, "/conversationlist.json") == 0) {
      handler = restful_meshms_conversationlist_json;
      remainder = "";
    } else if (*remainder == '/' && strn_to_sid_t(&r->sid2, remainder + 1, SIZE_MAX, &end) != -1) {
      remainder = end;
      if (strcmp(remainder, "/messagelist.json") == 0) {
	handler = restful_meshms_messagelist_json;
	remainder = "";
      }
      else if (   str_startswith(remainder, "/newsince/", &end)
	       && strn_to_meshms_token(end, &r->bid, &r->ui64, &end)
	       && strcmp(end, "/messagelist.json") == 0
      ) {
	handler = restful_meshms_newsince_messagelist_json;
	remainder = "";
      }
      else if (strcmp(remainder, "/sendmessage") == 0) {
	handler = restful_meshms_sendmessage;
	verb = HTTP_VERB_POST;
	remainder = "";
      }
    }
  }
  if (handler == NULL)
    return 404;
  if (r->http.verb != verb)
    return 405;
  int ret = authorize(&r->http);
  if (ret)
    return ret;
  ret = handler(r, remainder);
  return ret;
}

static HTTP_CONTENT_GENERATOR restful_meshms_conversationlist_json_content;

static int restful_meshms_conversationlist_json(httpd_request *r, const char *remainder)
{
  if (*remainder)
    return 404;
  assert(r->finalise_union == NULL);
  r->finalise_union = finalise_union_meshms_conversationlist;
  r->u.mclist.phase = LIST_HEADER;
  r->u.mclist.rowcount = 0;
  r->u.mclist.conv = NULL;
  enum meshms_status status;
  if (meshms_failed(status = meshms_conversations_list(&r->sid1, NULL, &r->u.mclist.conv)))
    return http_request_meshms_response(r, 0, NULL, status);
  meshms_conversation_iterator_start(&r->u.mclist.iter, r->u.mclist.conv);
  http_request_response_generated(&r->http, 200, CONTENT_TYPE_JSON, restful_meshms_conversationlist_json_content);
  return 1;
}

static HTTP_CONTENT_GENERATOR_STRBUF_CHUNKER restful_meshms_conversationlist_json_content_chunk;

static int restful_meshms_conversationlist_json_content(struct http_request *hr, unsigned char *buf, size_t bufsz, struct http_content_generator_result *result)
{
  return generate_http_content_from_strbuf_chunks(hr, (char *)buf, bufsz, result, restful_meshms_conversationlist_json_content_chunk);
}

static int restful_meshms_conversationlist_json_content_chunk(struct http_request *hr, strbuf b)
{
  httpd_request *r = (httpd_request *) hr;
  // The "my_sid" and "their_sid" per-conversation fields allow the same JSON structure to be used
  // in a future, non-SID-specific request, eg, to list all conversations for all currently open
  // identities.
  const char *headers[] = {
    "_id",
    "my_sid",
    "their_sid",
    "read",
    "last_message",
    "read_offset"
  };
  switch (r->u.mclist.phase) {
    case LIST_HEADER:
      strbuf_puts(b, "{\n\"header\":[");
      unsigned i;
      for (i = 0; i != NELS(headers); ++i) {
	if (i)
	  strbuf_putc(b, ',');
	strbuf_json_string(b, headers[i]);
      }
      strbuf_puts(b, "],\n\"rows\":[");
      if (!strbuf_overrun(b))
	r->u.mclist.phase = LIST_ROWS;
      return 1;
    case LIST_ROWS:
      if (r->u.mclist.iter.current == NULL) {
	r->u.mclist.phase = LIST_END;
	// fall through...
      } else {
	if (r->u.mclist.rowcount != 0)
	  strbuf_putc(b, ',');
	strbuf_puts(b, "\n[");
	strbuf_sprintf(b, "%zu", r->u.mclist.rowcount);
	strbuf_putc(b, ',');
	strbuf_json_hex(b, r->sid1.binary, sizeof r->sid1.binary);
	strbuf_putc(b, ',');
	strbuf_json_hex(b, r->u.mclist.iter.current->them.binary, sizeof r->u.mclist.iter.current->them.binary);
	strbuf_putc(b, ',');
	strbuf_json_boolean(b, r->u.mclist.iter.current->read_offset >= r->u.mclist.iter.current->their_last_message);
	strbuf_putc(b, ',');
	strbuf_sprintf(b, "%"PRIu64, r->u.mclist.iter.current->their_last_message);
	strbuf_putc(b, ',');
	strbuf_sprintf(b, "%"PRIu64, r->u.mclist.iter.current->read_offset);
	strbuf_puts(b, "]");
	if (!strbuf_overrun(b)) {
	  meshms_conversation_iterator_advance(&r->u.mclist.iter);
	  ++r->u.mclist.rowcount;
	}
	return 1;
      }
      // fall through...
    case LIST_END:
      strbuf_puts(b, "\n]\n}\n");
      if (!strbuf_overrun(b))
	r->u.mclist.phase = LIST_DONE;
      // fall through...
    case LIST_DONE:
      return 0;
  }
  abort();
  return 0;
}

static HTTP_CONTENT_GENERATOR restful_meshms_messagelist_json_content;

static enum meshms_status reopen_meshms_message_iterator(httpd_request *r)
{
  if (!meshms_message_iterator_is_open(&r->u.msglist.iter)) {
    enum meshms_status status;
    if (   meshms_failed(status = meshms_message_iterator_open(&r->u.msglist.iter, &r->sid1, &r->sid2))
	|| meshms_failed(status = meshms_message_iterator_prev(&r->u.msglist.iter))
    )
      return status;
    r->u.msglist.finished = status != MESHMS_STATUS_UPDATED;
    if (!r->u.msglist.finished) {
      r->u.msglist.latest_which_ply = r->u.msglist.iter.which_ply;
      r->u.msglist.latest_offset = r->u.msglist.iter.offset;
    }
  }
  return MESHMS_STATUS_OK;
}

static int restful_meshms_messagelist_json(httpd_request *r, const char *remainder)
{
  if (*remainder)
    return 404;
  assert(r->finalise_union == NULL);
  r->finalise_union = finalise_union_meshms_messagelist;
  r->u.msglist.rowcount = 0;
  r->u.msglist.phase = LIST_HEADER;
  r->u.msglist.token_offset = 0;
  r->u.msglist.end_time = 0;
  enum meshms_status status;
  if (meshms_failed(status = reopen_meshms_message_iterator(r)))
    return http_request_meshms_response(r, 0, NULL, status);
  http_request_response_generated(&r->http, 200, CONTENT_TYPE_JSON, restful_meshms_messagelist_json_content);
  return 1;
}

static int restful_meshms_newsince_messagelist_json(httpd_request *r, const char *remainder)
{
  if (*remainder)
    return 404;
  assert(r->finalise_union == NULL);
  r->finalise_union = finalise_union_meshms_messagelist;
  r->u.msglist.rowcount = 0;
  r->u.msglist.phase = LIST_HEADER;
  enum meshms_status status;
  if (meshms_failed(status = reopen_meshms_message_iterator(r)))
    return http_request_meshms_response(r, 0, NULL, status);
  if (cmp_rhizome_bid_t(&r->bid, r->u.msglist.iter.my_ply_bid) == 0)
    r->u.msglist.token_which_ply = MY_PLY;
  else if (cmp_rhizome_bid_t(&r->bid, r->u.msglist.iter.their_ply_bid) == 0)
    r->u.msglist.token_which_ply = THEIR_PLY;
  else {
    http_request_simple_response(&r->http, 404, "Unmatched token");
    return 404;
  }
  r->u.msglist.token_offset = r->ui64;
  r->u.msglist.end_time = gettime_ms() + config.rhizome.api.restful.newsince_timeout * 1000;
  http_request_response_generated(&r->http, 200, CONTENT_TYPE_JSON, restful_meshms_messagelist_json_content);
  return 1;
}

static HTTP_CONTENT_GENERATOR_STRBUF_CHUNKER restful_meshms_messagelist_json_content_chunk;

static int restful_meshms_messagelist_json_content(struct http_request *hr, unsigned char *buf, size_t bufsz, struct http_content_generator_result *result)
{
  httpd_request *r = (httpd_request *) hr;
  if (meshms_failed(reopen_meshms_message_iterator(r)))
    return -1;
  return generate_http_content_from_strbuf_chunks(hr, (char *)buf, bufsz, result, restful_meshms_messagelist_json_content_chunk);
}

static int restful_meshms_messagelist_json_content_chunk(struct http_request *hr, strbuf b)
{
  httpd_request *r = (httpd_request *) hr;
  // Include "my_sid" and "their_sid" per-message, so that the same JSON structure can be used by a
  // future, non-SID-specific request (eg, to get all messages for all currently open identities).
  const char *headers[] = {
    "type",
    "my_sid",
    "their_sid",
    "offset",
    "token",
    "text",
    "delivered",
    "read",
    "ack_offset"
  };
  switch (r->u.msglist.phase) {
    case LIST_HEADER:
      strbuf_puts(b, "{\n");
      if (!r->u.msglist.end_time) {
	strbuf_sprintf(b, "\"read_offset\":%"PRIu64",\n\"latest_ack_offset\":%"PRIu64",\n",
	    r->u.msglist.iter.read_offset,
	    r->u.msglist.iter.latest_ack_my_offset
	  );
      }
      strbuf_puts(b, "\"header\":[");
      unsigned i;
      for (i = 0; i != NELS(headers); ++i) {
	if (i)
	  strbuf_putc(b, ',');
	strbuf_json_string(b, headers[i]);
      }
      strbuf_puts(b, "],\n\"rows\":[");
      if (!strbuf_overrun(b))
	r->u.msglist.phase = r->u.msglist.finished ? LIST_END : LIST_ROWS;
      return 1;
    case LIST_ROWS:
      {
	if (   r->u.msglist.finished
	    || (r->u.msglist.token_which_ply == r->u.msglist.iter.which_ply && r->u.msglist.iter.offset <= r->u.msglist.token_offset)
	) {
	    time_ms_t now;
	    if (r->u.msglist.end_time && (now = gettime_ms()) < r->u.msglist.end_time) {
	      r->u.msglist.token_which_ply = r->u.msglist.latest_which_ply;
	      r->u.msglist.token_offset = r->u.msglist.latest_offset;
	      meshms_message_iterator_close(&r->u.msglist.iter);
	      time_ms_t wake_at = now + config.rhizome.api.restful.newsince_poll_ms;
	      if (wake_at > r->u.msglist.end_time)
		wake_at = r->u.msglist.end_time;
	      http_request_pause_response(&r->http, wake_at);
	      return 0;
	    }
	} else {
	  switch (r->u.msglist.iter.type) {
	    case MESSAGE_SENT:
	      if (r->u.msglist.rowcount != 0)
		strbuf_putc(b, ',');
	      strbuf_puts(b, "\n[");
	      strbuf_json_string(b, ">");
	      strbuf_putc(b, ',');
	      strbuf_json_hex(b, r->u.msglist.iter.my_sid->binary, sizeof r->u.msglist.iter.my_sid->binary);
	      strbuf_putc(b, ',');
	      strbuf_json_hex(b, r->u.msglist.iter.their_sid->binary, sizeof r->u.msglist.iter.their_sid->binary);
	      strbuf_putc(b, ',');
	      strbuf_sprintf(b, "%"PRIu64, r->u.msglist.iter.offset);
	      strbuf_putc(b, ',');
	      strbuf_json_string(b, alloca_meshms_token(&r->u.msglist.iter._conv->my_ply.bundle_id, r->u.msglist.iter.offset));
	      strbuf_putc(b, ',');
	      strbuf_json_string(b, r->u.msglist.iter.text);
	      strbuf_putc(b, ',');
	      strbuf_json_boolean(b, r->u.msglist.iter.delivered);
	      strbuf_putc(b, ',');
	      strbuf_json_boolean(b, 0);
	      strbuf_putc(b, ',');
	      strbuf_json_null(b);
	      strbuf_puts(b, "]");
	      break;
	    case MESSAGE_RECEIVED:
	      if (r->u.msglist.rowcount != 0)
		strbuf_putc(b, ',');
	      strbuf_puts(b, "\n[");
	      strbuf_json_string(b, "<");
	      strbuf_putc(b, ',');
	      strbuf_json_hex(b, r->u.msglist.iter.my_sid->binary, sizeof r->u.msglist.iter.my_sid->binary);
	      strbuf_putc(b, ',');
	      strbuf_json_hex(b, r->u.msglist.iter.their_sid->binary, sizeof r->u.msglist.iter.their_sid->binary);
	      strbuf_putc(b, ',');
	      strbuf_sprintf(b, "%"PRIu64, r->u.msglist.iter.offset);
	      strbuf_putc(b, ',');
	      strbuf_json_string(b, alloca_meshms_token(&r->u.msglist.iter._conv->their_ply.bundle_id, r->u.msglist.iter.offset));
	      strbuf_putc(b, ',');
	      strbuf_json_string(b, r->u.msglist.iter.text);
	      strbuf_putc(b, ',');
	      strbuf_json_boolean(b, 1);
	      strbuf_putc(b, ',');
	      strbuf_json_boolean(b, r->u.msglist.iter.read);
	      strbuf_putc(b, ',');
	      strbuf_json_null(b);
	      strbuf_puts(b, "]");
	      break;
	    case ACK_RECEIVED:
	      // Don't send old (irrelevant) ACKs.
	      if (r->u.msglist.iter.ack_offset > r->u.msglist.highest_ack_offset) {
		if (r->u.msglist.rowcount != 0)
		  strbuf_putc(b, ',');
		strbuf_puts(b, "\n[");
		strbuf_json_string(b, "ACK");
		strbuf_putc(b, ',');
		strbuf_json_hex(b, r->u.msglist.iter.my_sid->binary, sizeof r->u.msglist.iter.my_sid->binary);
		strbuf_putc(b, ',');
		strbuf_json_hex(b, r->u.msglist.iter.their_sid->binary, sizeof r->u.msglist.iter.their_sid->binary);
		strbuf_putc(b, ',');
		strbuf_sprintf(b, "%"PRIu64, r->u.msglist.iter.offset);
		strbuf_putc(b, ',');
		strbuf_json_string(b, alloca_meshms_token(&r->u.msglist.iter._conv->their_ply.bundle_id, r->u.msglist.iter.offset));
		strbuf_putc(b, ',');
		strbuf_json_string(b, r->u.msglist.iter.text);
		strbuf_putc(b, ',');
		strbuf_json_boolean(b, 1);
		strbuf_putc(b, ',');
		strbuf_json_boolean(b, r->u.msglist.iter.read);
		strbuf_putc(b, ',');
		strbuf_sprintf(b, "%"PRIu64, r->u.msglist.iter.ack_offset);
		strbuf_puts(b, "]");
		r->u.msglist.highest_ack_offset = r->u.msglist.iter.ack_offset;
	      }
	      break;
	  }
	  if (!strbuf_overrun(b)) {
	    ++r->u.msglist.rowcount;
	    enum meshms_status status;
	    if (meshms_failed(status = meshms_message_iterator_prev(&r->u.msglist.iter)))
	      return http_request_meshms_response(r, 0, NULL, status);
	    r->u.msglist.finished = status != MESHMS_STATUS_UPDATED;
	  }
	  return 1;
	}
	r->u.msglist.phase = LIST_END;
      }
      // fall through...
    case LIST_END:
      strbuf_puts(b, "\n]\n}\n");
      if (!strbuf_overrun(b))
	r->u.msglist.phase = LIST_DONE;
      // fall through...
    case LIST_DONE:
      return 0;
  }
  abort();
  return 0;
}

static HTTP_REQUEST_PARSER restful_meshms_sendmessage_end;
static int send_mime_part_start(struct http_request *);
static int send_mime_part_end(struct http_request *);
static int send_mime_part_header(struct http_request *, const struct mime_part_headers *);
static int send_mime_part_body(struct http_request *, char *, size_t);

static int restful_meshms_sendmessage(httpd_request *r, const char *remainder)
{
  if (*remainder)
    return 404;
  assert(r->finalise_union == NULL);
  r->finalise_union = finalise_union_meshms_sendmessage;
  // Parse the request body as multipart/form-data.
  assert(r->u.sendmsg.current_part == NULL);
  assert(!r->u.sendmsg.received_message);
  r->http.form_data.handle_mime_part_start = send_mime_part_start;
  r->http.form_data.handle_mime_part_end = send_mime_part_end;
  r->http.form_data.handle_mime_part_header = send_mime_part_header;
  r->http.form_data.handle_mime_body = send_mime_part_body;
  // Send the message once the body has arrived.
  r->http.handle_content_end = restful_meshms_sendmessage_end;
  return 1;
}

static char PART_MESSAGE[] = "message";

static int send_mime_part_start(struct http_request *hr)
{
  httpd_request *r = (httpd_request *) hr;
  assert(r->u.sendmsg.current_part == NULL);
  return 0;
}

static int send_mime_part_end(struct http_request *hr)
{
  httpd_request *r = (httpd_request *) hr;
  if (r->u.sendmsg.current_part == PART_MESSAGE) {
    if (r->u.sendmsg.message.length == 0)
      return http_response_form_part(r, "Invalid (empty)", PART_MESSAGE, NULL, 0);
    r->u.sendmsg.received_message = 1;
    if (config.debug.httpd)
      DEBUGF("received %s = %s", PART_MESSAGE, alloca_toprint(-1, r->u.sendmsg.message.buffer, r->u.sendmsg.message.length));
  } else
    FATALF("current_part = %s", alloca_str_toprint(r->u.sendmsg.current_part));
  r->u.sendmsg.current_part = NULL;
  return 0;
}

static int send_mime_part_header(struct http_request *hr, const struct mime_part_headers *h)
{
  httpd_request *r = (httpd_request *) hr;
  if (strcmp(h->content_disposition.type, "form-data") != 0)
    return http_response_content_disposition(r, "Unsupported", h->content_disposition.type);
  if (strcmp(h->content_disposition.name, PART_MESSAGE) == 0) {
    if (r->u.sendmsg.received_message)
      return http_response_form_part(r, "Duplicate", PART_MESSAGE, NULL, 0);
    r->u.sendmsg.current_part = PART_MESSAGE;
    form_buf_malloc_init(&r->u.sendmsg.message, MESHMS_MESSAGE_MAX_LEN);
  }
  else
    return http_response_form_part(r, "Unsupported", h->content_disposition.name, NULL, 0);
  if (!h->content_type.type[0] || !h->content_type.subtype[0])
    return http_response_content_type(r, "Missing", &h->content_type);
  if (strcmp(h->content_type.type, "text") != 0 || strcmp(h->content_type.subtype, "plain") != 0)
    return http_response_content_type(r, "Unsupported", &h->content_type);
  if (!h->content_type.charset[0])
    return http_response_content_type(r, "Missing charset", &h->content_type);
  if (strcmp(h->content_type.charset, "utf-8") != 0)
    return http_response_content_type(r, "Unsupported charset", &h->content_type);
  return 0;
}

static int send_mime_part_body(struct http_request *hr, char *buf, size_t len)
{
  httpd_request *r = (httpd_request *) hr;
  if (r->u.sendmsg.current_part == PART_MESSAGE) {
    form_buf_malloc_accumulate(r, PART_MESSAGE, &r->u.sendmsg.message, buf, len);
  } else
    FATALF("current_part = %s", alloca_str_toprint(r->u.sendmsg.current_part));
  return 0;
}

static int restful_meshms_sendmessage_end(struct http_request *hr)
{
  httpd_request *r = (httpd_request *) hr;
  if (!r->u.sendmsg.received_message)
    return http_response_form_part(r, "Missing", PART_MESSAGE, NULL, 0);
  assert(r->u.sendmsg.message.length > 0);
  assert(r->u.sendmsg.message.length <= MESHMS_MESSAGE_MAX_LEN);
  enum meshms_status status;
  if (meshms_failed(status = meshms_send_message(&r->sid1, &r->sid2, r->u.sendmsg.message.buffer, r->u.sendmsg.message.length)))
    return http_request_meshms_response(r, 0, NULL, status);
  return http_request_meshms_response(r, 201, "Message sent", status);
}
