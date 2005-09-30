/*
 * Copyright (c) 2005 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * XMLRPC account management functions.
 *
 * $Id: account.c 2455 2005-09-30 01:22:43Z nenolod $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"xmlrpc/account", FALSE, _modinit, _moddeinit,
	"$Id: account.c 2455 2005-09-30 01:22:43Z nenolod $",
	"Atheme Development Group <http://www.atheme.org>"
);

boolean_t using_nickserv = FALSE;

/*
 * atheme.register_account
 *
 * XML inputs:
 *       account to register, password, email.
 *
 * XML outputs:
 *       fault 1 - account already exists, please try again
 *       fault 2 - password != account, try again
 *       fault 3 - invalid email address
 *       fault 4 - not enough parameters
 *       fault 5 - user is on IRC (would be unfair to claim ownership)
 *       fault 6 - too many accounts associated with this email
 *       default - success message
 *
 * Side Effects:
 *       an account is registered in the system
 */
static int register_account(int parc, char *parv[])
{
	user_t *u;
	myuser_t *mu, *tmu;
	node_t *n;
	uint32_t i, tcnt;
	static char buf[XMLRPC_BUFSIZE];

	*buf = '\0';

	if (parc < 3)
	{
		xmlrpc_generic_error(4, "Insufficient parameters.");
		return 0;
	}

	if (using_nickserv == TRUE && (u = user_find(parv[0])) != NULL)
	{
		xmlrpc_generic_error(5, "A user matching this account is already on IRC.");
		return 0;
	}

	if (!strcasecmp(parv[0], parv[1]))
	{
		xmlrpc_generic_error(2, "You cannot use your account name as a password.");
		return 0;
	}

	if (!validemail(parv[2]))
	{
		xmlrpc_generic_error(3, "invalid email address");
		return 0;
	}

	if ((mu = myuser_find(parv[0])) != NULL)
	{
		xmlrpc_generic_error(1, "account already exists");
		return 0;
	}

	for (i = 0, tcnt = 0; i < HASHSIZE; i++)
	{
		LIST_FOREACH(n, mulist[i].head)
		{
			tmu = (myuser_t *)n->data;

			if (!strcasecmp(parv[2], tmu->email))
				tcnt++;
		}
	}

	if (tcnt >= me.maxusers)
	{
		xmlrpc_generic_error(6, "too many accounts registered for this email.");
		return 0;
	}

	snoop("REGISTER: \2%s\2 to \2%s\2 (via \2xmlrpc\2)", parv[0], parv[2]);

	mu = myuser_add(parv[0], parv[1], parv[2]);
	mu->registered = CURRTIME;
	mu->lastlogin = CURRTIME;
	mu->flags |= config_options.defuflags;

	if (me.auth == AUTH_EMAIL)
	{
		char *key = gen_pw(12);
		mu->flags |= MU_WAITAUTH;

		metadata_add(mu, METADATA_USER, "private:verify:register:key", key);
		metadata_add(mu, METADATA_USER, "private:verify:register:timestamp", itoa(time(NULL)));

		xmlrpc_string(buf, "An email containing nickname activiation instructions has been sent to your email address.");
		xmlrpc_string(buf, "If you do not complete registration within one day your nickname will expire.");

		sendemail(mu->name, key, 1);

		free(key);
	}

	xmlrpc_string(buf, "Registration successful.");
	xmlrpc_send(1, buf);
	return 0;
}

/*
 * atheme.verify_account
 *
 * XML inputs:
 *       requested operation, account name, key
 *
 * XML outputs:
 *       fault 1 - the account is not registered
 *       fault 2 - the operation has already been verified
 *       fault 3 - invalid verification key for this operation
 *       fault 4 - insufficient parameters
 *       fault 5 - invalid operation requested
 *       default - success
 *
 * Side Effects:
 *       an account-related operation is verified.
 */      
static int verify_account(int parc, char *parv[])
{
	myuser_t *mu;
	metadata_t *md;
	char buf[XMLRPC_BUFSIZE];

	if (parc < 3)
	{
		xmlrpc_generic_error(4, "Insufficient parameters.");
		return 0;
	}

	if (!(mu = myuser_find(parv[1])))
	{
		xmlrpc_generic_error(1, "The account is not registered.");
		return 0;
	}

	if (!strcasecmp(parv[0], "REGISTER"))
	{
		metadata_t *md;

		if (!(mu->flags & MU_WAITAUTH) || !(md = metadata_find(mu, METADATA_USER, "private:verify:register:key")))
		{
			xmlrpc_generic_error(2, "The operation has already been verified.");
			return 0;
		}

		if (!strcasecmp(key, md->value))
		{
			mu->flags &= ~MU_WAITAUTH;

			snoop("REGISTER:VS: \2%s\2 via xmlrpc", mu->email);

			metadata_delete(mu, METADATA_USER, "private:verify:register:key");
			metadata_delete(mu, METADATA_USER, "private:verify:register:timestamp");

			xmlrpc_string(buf, "Registration verification was successful.");
			xmlrpc_send(1, buf);
			return 0;
		}

		snoop("REGISTER:VF: \2%s\2 via xmlrpc", mu->email);
		xmlrpc_generic_error(3, "Invalid key for this operation.");
		return 0;
	}
	else if (!strcasecmp(op, "EMAILCHG"))
	{
		if (!(md = metadata_find(mu, METADATA_USER, "private:verify:emailchg:key")))
		{
			xmlrpc_generic_error(2, "The operation has already been verified.");
			return 0;
		}

		if (!strcasecmp(key, md->value))
                {
			md = metadata_find(mu, METADATA_USER, "private:verify:emailchg:newemail");

			strlcpy(mu->email, md->value, EMAILLEN);

			snoop("SET:EMAIL:VS: \2%s\2 via xmlrpc", mu->email);

			metadata_delete(mu, METADATA_USER, "private:verify:emailchg:key");
			metadata_delete(mu, METADATA_USER, "private:verify:emailchg:newemail");
			metadata_delete(mu, METADATA_USER, "private:verify:emailchg:timestamp");

			xmlrpc_string(buf, "E-Mail change verification was successful.");
			xmlrpc_send(1, buf);

			return 0;
                }

		snoop("REGISTER:VF: \2%s\2 by \2%s\2", mu->email, origin);
		xmlrpc_generic_error(3, "Invalid key for this operation.");

		return 0;
	}
	else
	{
		xmlrpc_generic_error(5, "Invalid verification operation requested.");
		return 0;
	}

	return 0;
}

/*
 * atheme.login
 *
 * XML Inputs:
 *       account name and password
 *
 * XML Outputs:
 *       fault 1 - account is not registered
 *       fault 2 - invalid username and password
 *       fault 4 - insufficient parameters
 *       default - success (authcookie)
 *
 * Side Effects:
 *       an authcookie ticket is created for the myuser_t.
 */
static int do_login(int parc, char *parv[])
{
	myuser_t *mu;
	char buf[BUFSIZE];

	if (parc < 2)
	{
		xmlrpc_generic_error(4, "Insufficient parameters.");
		return 0;
	}

	if (!(mu = myuser_find(parv[0])))
	{
		xmlrpc_generic_error(1, "The account is not registered.");
		return 0;
	}

	if (strcmp(mu->pass, parv[1]))
	{
		xmlrpc_generic_error(2, "The password is not valid for this account.");
		return 0;
	}

	xmlrpc_string(buf, authcookie_create(mu));
	xmlrpc_send(1, buf);

	return 0;
}

void _modinit(module_t *m)
{
	if (module_find_published("nickserv/main"))
		using_nickserv = TRUE;

	xmlrpc_register_method("atheme.register_account", register_account);
	xmlrpc_register_method("atheme.verify_account", verify_account);	
	xmlrpc_register_method("atheme.login", do_login);
}

void _moddeinit(void)
{
	xmlrpc_unregister_method("atheme.register_account");
	xmlrpc_unregister_method("atheme.verify_account");
	xmlrpc_unregister_method("atheme.login");
}
