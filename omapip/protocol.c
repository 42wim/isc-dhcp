/* protocol.c

   Functions supporting the object management protocol... */

/*
 * Copyright (c) 1996-1999 Internet Software Consortium.
 * Use is subject to license terms which appear in the file named
 * ISC-LICENSE that should have accompanied this file when you
 * received it.   If a file named ISC-LICENSE did not accompany this
 * file, or you are not sure the one you have is correct, you may
 * obtain an applicable copy of the license at:
 *
 *             http://www.isc.org/isc-license-1.0.html. 
 *
 * This file is part of the ISC DHCP distribution.   The documentation
 * associated with this file is listed in the file DOCUMENTATION,
 * included in the top-level directory of this release.
 *
 * Support and other services are available for ISC products - see
 * http://www.isc.org for more information.
 */

#include <omapip/omapip.h>

isc_result_t omapi_protocol_connect (omapi_object_t *h,
				     char *server_name,
				     int port,
				     omapi_object_t *authinfo)
{
	isc_result_t status;
	omapi_protocol_object_t *obj;

	obj = (omapi_protocol_object_t *)malloc (sizeof *obj);
	if (!obj)
		return ISC_R_NOMEMORY;
	memset (obj, 0, sizeof *obj);
	obj -> refcnt = 1;
	obj -> type = omapi_type_protocol;

	status = omapi_connect ((omapi_object_t *)obj, server_name, port);
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_connect");
		return status;
	}
	status = omapi_object_reference (&h -> outer, (omapi_object_t *)obj,
					 "omapi_protocol_connect");
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_connect");
		return status;
	}
	status = omapi_object_reference (&obj -> inner, h,
					 "omapi_protocol_connect");
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_connect");
		return status;
	}

	/* Send the introductory message. */
	status = omapi_protocol_send_intro ((omapi_object_t *)obj,
					    OMAPI_PROTOCOL_VERSION,
					    sizeof (omapi_protocol_header_t));
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_connect");
		return status;
	}

	if (authinfo)
		omapi_object_reference (&obj -> authinfo, authinfo,
					"omapi_protocol_connect");
	omapi_object_dereference ((omapi_object_t **)&obj,
				  "omapi_protocol_accept");
	return ISC_R_SUCCESS;
}

/* Send the protocol introduction message. */
isc_result_t omapi_protocol_send_intro (omapi_object_t *h,
					int ver,
					int hsize)
{
	isc_result_t status;
	omapi_protocol_object_t *p;

	if (h -> type != omapi_type_protocol)
		return ISC_R_INVALIDARG;
	p = (omapi_protocol_object_t *)h;

	if (!h -> outer || h -> outer -> type != omapi_type_connection)
		return ISC_R_NOTCONNECTED;

	status = omapi_connection_put_uint32 (h -> outer, ver);
	if (status != ISC_R_SUCCESS)
		return status;

	status = omapi_connection_put_uint32 (h -> outer, hsize);

	if (status != ISC_R_SUCCESS)
		return status;

	/* Require the other end to send an intro - this kicks off the
	   protocol input state machine. */
	p -> state = omapi_protocol_intro_wait;
	status = omapi_connection_require (h -> outer, 8);
	if (status != ISC_R_SUCCESS && status != ISC_R_NOTYET)
		return status;

	/* Make up an initial transaction ID for this connection. */
	p -> next_xid = random ();
	return ISC_R_SUCCESS;
}

isc_result_t omapi_protocol_send_message (omapi_object_t *po,
					  omapi_object_t *id,
					  omapi_object_t *mo,
					  omapi_object_t *omo)
{
	omapi_protocol_object_t *p;
	omapi_object_t *c;
	omapi_message_object_t *m;
	omapi_message_object_t *om;
	isc_result_t status;
	u_int32_t foo;

	if (po -> type != omapi_type_protocol ||
	    !po -> outer || po -> outer -> type != omapi_type_connection ||
	    mo -> type != omapi_type_message)
		return ISC_R_INVALIDARG;
	if (omo && omo -> type != omapi_type_message)
		return ISC_R_INVALIDARG;
	p = (omapi_protocol_object_t *)po;
	c = (omapi_object_t *)(po -> outer);
	m = (omapi_message_object_t *)mo;
	om = (omapi_message_object_t *)omo;

	/* XXX Write the authenticator length */
	status = omapi_connection_put_uint32 (c, 0);
	if (status != ISC_R_SUCCESS)
		return status;
	/* XXX Write the ID of the authentication key we're using. */
	status = omapi_connection_put_uint32 (c, 0);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Write the opcode. */
	status = omapi_connection_put_uint32 (c, m -> op);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Write the handle.  If we've been given an explicit handle, use
	   that.   Otherwise, use the handle of the object we're sending.
	   The caller is responsible for arranging for one of these handles
	   to be set (or not). */
	status = omapi_connection_put_uint32 (c, (m -> h
						  ? m -> h
						  : m -> object -> handle));
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Set and write the transaction ID. */
	m -> id = p -> next_xid++;
	status = omapi_connection_put_uint32 (c, m -> id);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Write the transaction ID of the message to which this is a
	   response, if there is such a message. */
	status = omapi_connection_put_uint32 (c, om ? om -> id : 0);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Stuff out the name/value pairs specific to this message. */
	if (m -> object) {
		status = omapi_stuff_values (c, id, (omapi_object_t *)m);
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return status;
		}
	}

	/* Write the zero-length name that terminates the list of name/value
	   pairs specific to the message. */
	status = omapi_connection_put_uint16 (c, 0);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* Stuff out all the published name/value pairs in the object that's
	   being sent in the message, if there is one. */
	if (m -> object) {
		status = omapi_stuff_values (c, id, m -> object);
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return status;
		}
	}

	/* Write the zero-length name that terminates the list of name/value
	   pairs for the associated object. */
	status = omapi_connection_put_uint16 (c, 0);
	if (status != ISC_R_SUCCESS) {
		omapi_disconnect (c, 1);
		return status;
	}

	/* XXX Write the authenticator... */

	return ISC_R_SUCCESS;
}
					  

isc_result_t omapi_protocol_signal_handler (omapi_object_t *h,
					    char *name, va_list ap)
{
	isc_result_t status;
	omapi_protocol_object_t *p;
	omapi_object_t *c;
	u_int16_t nlen;
	u_int32_t vlen;

	if (h -> type != omapi_type_protocol) {
		/* XXX shouldn't happen.   Put an assert here? */
		return ISC_R_UNEXPECTED;
	}
	p = (omapi_protocol_object_t *)h;

	/* Not a signal we recognize? */
	if (strcmp (name, "ready")) {
		if (p -> inner && p -> inner -> type -> signal_handler)
			return (*(p -> inner -> type -> signal_handler)) (h,
									  name,
									  ap);
		return ISC_R_NOTFOUND;
	}

	if (!p -> outer || p -> outer -> type != omapi_type_connection)
		return ISC_R_INVALIDARG;
	c = p -> outer;

	/* We get here because we requested that we be woken up after
           some number of bytes were read, and that number of bytes
           has in fact been read. */
	switch (p -> state) {
	      case omapi_protocol_intro_wait:
		/* Get protocol version and header size in network
		   byte order. */
		omapi_connection_get_uint32 (c, &p -> protocol_version);
		omapi_connection_get_uint32 (c, &p -> header_size);
	
		/* We currently only support the current protocol version. */
		if (p -> protocol_version != OMAPI_PROTOCOL_VERSION) {
			omapi_disconnect (c, 1);
			return ISC_R_VERSIONMISMATCH;
		}

		if (p -> header_size < sizeof (omapi_protocol_header_t)) {
			omapi_disconnect (c, 1);
			return ISC_R_PROTOCOLERROR;
		}

		status = omapi_signal_in (h -> inner, "ready");

	      to_header_wait:
		/* The next thing we're expecting is a message header. */
		p -> state = omapi_protocol_header_wait;

		/* Register a need for the number of bytes in a
		   header, and if we already have that many, process
		   them immediately. */
		if ((omapi_connection_require
		     (c, p -> header_size)) != ISC_R_SUCCESS)
			break;
		/* If we already have the data, fall through. */

	      case omapi_protocol_header_wait:
		status = omapi_message_new ((omapi_object_t **)&p -> message,
					    "omapi_protocol_signal_handler");
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return status;
		}

		/* Swap in the header... */
		omapi_connection_get_uint32 (c, &p -> message -> authid);

		/* XXX bind the authenticator here! */
		omapi_connection_get_uint32 (c, &p -> message -> authlen);
		omapi_connection_get_uint32 (c, &p -> message -> op);
		omapi_connection_get_uint32 (c, &p -> message -> handle);
		omapi_connection_get_uint32 (c, &p -> message -> id);
		omapi_connection_get_uint32 (c, &p -> message -> rid);

		/* If there was any extra header data, skip over it. */
		if (p -> header_size > sizeof (omapi_protocol_header_t)) {
			omapi_connection_copyout
				(0, c, (p -> header_size -
					sizeof (omapi_protocol_header_t)));
		}
						     
		/* XXX must compute partial signature across the
                   XXX preceding bytes.    Also, if authenticator
		   specifies encryption as well as signing, we may
		   have to decrypt the data on the way in. */

		/* First we read in message-specific values, then object
		   values. */
		p -> reading_message_values = 1;

	      need_name_length:
		/* The next thing we're expecting is length of the
		   first name. */
		p -> state = omapi_protocol_name_length_wait;

		/* Wait for a 16-bit length. */
		if ((omapi_connection_require (c, 2)) != ISC_R_SUCCESS)
			break;
		/* If it's already here, fall through. */

	      case omapi_protocol_name_length_wait:
		omapi_connection_get_uint16 (c, &nlen);
		/* A zero-length name means that we're done reading name+value
		   pairs. */
		if (nlen == 0) {
			/* If we've already read in the object, we are
			   done reading the message, but if we've just
			   finished reading in the values associated
			   with the message, we need to read the
			   object. */
			if (p -> reading_message_values) {
				p -> reading_message_values = 0;
				goto need_name_length;
			}

			/* If the authenticator length is zero, there's no
			   signature to read in, so go straight to processing
			   the message. */
			if (p -> message -> authlen == 0)
				goto message_done;

			/* The next thing we're expecting is the
                           message signature. */
			p -> state = omapi_protocol_signature_wait;

			/* Wait for the number of bytes specified for
			   the authenticator.  If we already have it,
			   go read it in. */
			if (omapi_connection_require
			    (c, p -> message -> authlen) == ISC_R_SUCCESS)
				goto signature_wait;
			break;
		}

		/* Allocate a buffer for the name. */
		status = (omapi_data_string_new
			  (&p -> name, nlen, "omapi_protocol_signal_handler"));
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return ISC_R_NOMEMORY;
		}
		p -> state = omapi_protocol_name_wait;
		if (omapi_connection_require (c, nlen) != ISC_R_SUCCESS)
			break;
		/* If it's already here, fall through. */
					     
	      case omapi_protocol_name_wait:
		omapi_connection_copyout (p -> name -> value, c,
					  p -> name -> len);
		/* Wait for a 32-bit length. */
		p -> state = omapi_protocol_value_length_wait;
		if ((omapi_connection_require (c, 4)) != ISC_R_SUCCESS)
			break;
		/* If it's already here, fall through. */

	      case omapi_protocol_value_length_wait:
		omapi_connection_get_uint32 (c, &vlen);

		/* Zero-length values are allowed - if we get one, we
		   don't have to read any data for the value - just
		   get the next one, if there is a next one. */
		if (!vlen)
			goto insert_new_value;

		status = (omapi_typed_data_new
			  (&p -> value, omapi_datatype_data, vlen,
			   "omapi_protocol_signal_handler"));
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return ISC_R_NOMEMORY;
		}

		p -> state = omapi_protocol_value_wait;
		if (omapi_connection_require (c, vlen) != ISC_R_SUCCESS)
			break;
		/* If it's already here, fall through. */
					     
	      case omapi_protocol_value_wait:
		omapi_connection_copyout (p -> value -> u.buffer.value, c,
					  p -> value -> u.buffer.len);

	      insert_new_value:
		if (p -> reading_message_values) {
			status = (omapi_set_value
				  ((omapi_object_t *)p -> message,
				   p -> message -> id_object,
				   p -> name, p -> value));
		} else {
			if (!p -> message -> object) {
				/* We need a generic object to hang off of the
				   incoming message. */
				status = (omapi_generic_new
					  (&p -> message -> object,
					   "omapi_protocol_signal_handler"));
				if (status != ISC_R_SUCCESS) {
					omapi_disconnect (c, 1);
					return status;
				}
			}
			status = (omapi_set_value
				  ((omapi_object_t *)p -> message -> object,
				   p -> message -> id_object,
				   p -> name, p -> value));
		}
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return status;
		}
		omapi_data_string_dereference
			(&p -> name, "omapi_protocol_signal_handler");
		omapi_typed_data_dereference (&p -> value,
					      "omapi_protocol_signal_handler");
		goto need_name_length;

	      signature_wait:
	      case omapi_protocol_signature_wait:
		status = omapi_typed_data_new (&p -> message -> authenticator,
					       omapi_datatype_data,
					       p -> message -> authlen);
			
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return ISC_R_NOMEMORY;
		}
		omapi_connection_copyout
			(p -> message -> authenticator -> u.buffer.value, c,
			 p -> message -> authlen);
		/* XXX now do something to verify the signature. */

		/* Process the message. */
	      message_done:
		status = omapi_message_process (p -> message);
		if (status != ISC_R_SUCCESS) {
			omapi_disconnect (c, 1);
			return ISC_R_NOMEMORY;
		}

		/* XXX unbind the authenticator. */
	      auth_unbind:
		omapi_object_dereference ((omapi_object_t **)&p -> message,
					  "omapi_protocol_signal_handler");

		/* Now wait for the next message. */
		goto to_header_wait;		

	      default:
		/* XXX should never get here.   Assertion? */
	}
	return ISC_R_SUCCESS;
}

isc_result_t omapi_protocol_set_value (omapi_object_t *h,
				       omapi_object_t *id,
				       omapi_data_string_t *name,
				       omapi_typed_data_t *value)
{
	if (h -> type != omapi_type_protocol)
		return ISC_R_INVALIDARG;

	if (h -> inner && h -> inner -> type -> set_value)
		return (*(h -> inner -> type -> set_value))
			(h -> inner, id, name, value);
	return ISC_R_NOTFOUND;
}

isc_result_t omapi_protocol_get_value (omapi_object_t *h,
				       omapi_object_t *id,
				       omapi_data_string_t *name,
				       omapi_value_t **value)
{
	if (h -> type != omapi_type_protocol)
		return ISC_R_INVALIDARG;
	
	if (h -> inner && h -> inner -> type -> get_value)
		return (*(h -> inner -> type -> get_value))
			(h -> inner, id, name, value);
	return ISC_R_NOTFOUND;
}

isc_result_t omapi_protocol_destroy (omapi_object_t *h, char *name)
{
	omapi_protocol_object_t *p;
	if (h -> type != omapi_type_protocol)
		return ISC_R_INVALIDARG;
	p = (omapi_protocol_object_t *)h;
	if (p -> message)
		omapi_object_dereference ((omapi_object_t **)&p -> message,
					  name);
	if (p -> authinfo)
		return omapi_object_dereference (&p -> authinfo, name);
	return ISC_R_SUCCESS;
}

/* Write all the published values associated with the object through the
   specified connection. */

isc_result_t omapi_protocol_stuff_values (omapi_object_t *c,
					  omapi_object_t *id,
					  omapi_object_t *p)
{
	int i;

	if (p -> type != omapi_type_protocol)
		return ISC_R_INVALIDARG;

	if (p -> inner && p -> inner -> type -> stuff_values)
		return (*(p -> inner -> type -> stuff_values)) (c, id,
								p -> inner);
	return ISC_R_SUCCESS;
}

/* Set up a listener for the omapi protocol.    The handle stored points to
   a listener object, not a protocol object. */

isc_result_t omapi_protocol_listen (omapi_object_t *h,
				    int port,
				    int max)
{
	isc_result_t status;
	omapi_protocol_listener_object_t *obj;

	obj = (omapi_protocol_listener_object_t *)malloc (sizeof *obj);
	if (!obj)
		return ISC_R_NOMEMORY;
	memset (obj, 0, sizeof *obj);
	obj -> refcnt = 1;
	obj -> type = omapi_type_protocol_listener;

	status = omapi_object_reference (&h -> outer, (omapi_object_t *)obj,
					 "omapi_protocol_listen");
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_listen");
		return status;
	}
	status = omapi_object_reference (&obj -> inner, h,
					 "omapi_protocol_listen");
	if (status != ISC_R_SUCCESS) {
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_listen");
		return status;
	}

	status = omapi_listen ((omapi_object_t *)obj, port, max);
	omapi_object_dereference ((omapi_object_t **)&obj,
				  "omapi_protocol_listen");
	return status;
}

/* Signal handler for protocol listener - if we get a connect signal,
   create a new protocol connection, otherwise pass the signal down. */

isc_result_t omapi_protocol_listener_signal (omapi_object_t *o,
					     char *name, va_list ap)
{
	isc_result_t status;
	omapi_object_t *c;
	omapi_protocol_object_t *obj;
	omapi_protocol_listener_object_t *p;

	if (!o || o -> type != omapi_type_protocol_listener)
		return ISC_R_INVALIDARG;
	p = (omapi_protocol_listener_object_t *)o;

	/* Not a signal we recognize? */
	if (strcmp (name, "connect")) {
		if (p -> inner && p -> inner -> type -> signal_handler)
			return (*(p -> inner -> type -> signal_handler))
				(p -> inner, name, ap);
		return ISC_R_NOTFOUND;
	}

	c = va_arg (ap, omapi_object_t *);
	if (!c || c -> type != omapi_type_connection)
		return ISC_R_INVALIDARG;

	obj = (omapi_protocol_object_t *)malloc (sizeof *obj);
	if (!obj)
		return ISC_R_NOMEMORY;
	memset (obj, 0, sizeof *obj);
	obj -> refcnt = 1;
	obj -> type = omapi_type_protocol;

	status = omapi_object_reference (&obj -> outer, c,
					 "omapi_protocol_accept");
	if (status != ISC_R_SUCCESS) {
	      lose:
		omapi_object_dereference ((omapi_object_t **)&obj,
					  "omapi_protocol_accept");
		omapi_disconnect (c, 1);
		return status;
	}

	status = omapi_object_reference (&c -> inner, (omapi_object_t *)obj,
					 "omapi_protocol_accept");
	if (status != ISC_R_SUCCESS)
		goto lose;

	/* Send the introductory message. */
	status = omapi_protocol_send_intro ((omapi_object_t *)obj,
					    OMAPI_PROTOCOL_VERSION,
					    sizeof (omapi_protocol_header_t));
	if (status != ISC_R_SUCCESS)
		goto lose;

	omapi_object_dereference ((omapi_object_t **)&obj,
				  "omapi_protocol_accept");
	return status;
}

isc_result_t omapi_protocol_listener_set_value (omapi_object_t *h,
						omapi_object_t *id,
						omapi_data_string_t *name,
						omapi_typed_data_t *value)
{
	if (h -> type != omapi_type_protocol_listener)
		return ISC_R_INVALIDARG;
	
	if (h -> inner && h -> inner -> type -> set_value)
		return (*(h -> inner -> type -> set_value))
			(h -> inner, id, name, value);
	return ISC_R_NOTFOUND;
}

isc_result_t omapi_protocol_listener_get_value (omapi_object_t *h,
						omapi_object_t *id,
						omapi_data_string_t *name,
						omapi_value_t **value)
{
	if (h -> type != omapi_type_protocol_listener)
		return ISC_R_INVALIDARG;
	
	if (h -> inner && h -> inner -> type -> get_value)
		return (*(h -> inner -> type -> get_value))
			(h -> inner, id, name, value);
	return ISC_R_NOTFOUND;
}

isc_result_t omapi_protocol_listener_destroy (omapi_object_t *h, char *name)
{
	if (h -> type != omapi_type_protocol_listener)
		return ISC_R_INVALIDARG;
	return ISC_R_SUCCESS;
}

/* Write all the published values associated with the object through the
   specified connection. */

isc_result_t omapi_protocol_listener_stuff (omapi_object_t *c,
					    omapi_object_t *id,
					    omapi_object_t *p)
{
	int i;

	if (p -> type != omapi_type_protocol_listener)
		return ISC_R_INVALIDARG;

	if (p -> inner && p -> inner -> type -> stuff_values)
		return (*(p -> inner -> type -> stuff_values)) (c, id,
								p -> inner);
	return ISC_R_SUCCESS;
}

