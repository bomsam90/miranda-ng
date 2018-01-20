////////////////////////////////////////////////////////////////////////////////
// Gadu-Gadu Plugin for Miranda IM
//
// Copyright (c) 2010 Bartosz Białek
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
////////////////////////////////////////////////////////////////////////////////

#include "gg.h"
#include <io.h>
#include <fcntl.h>
#include "protocol.h"

//////////////////////////////////////////////////////////
// OAuth 1.0 implementation

// Service Provider must accept the HTTP Authorization header

// RSA-SHA1 signature method (see RFC 3447 section 8.2
// and RSASSA-PKCS1-v1_5 algorithm) is unimplemented

struct OAUTHPARAMETER
{
	char *name;
	char *value;
};

enum OAUTHSIGNMETHOD
{
	HMACSHA1,
	RSASHA1,
	PLAINTEXT
};

static int paramsortFunc(const OAUTHPARAMETER *p1, const OAUTHPARAMETER *p2)
{
	int res = mir_strcmp(p1->name, p2->name);
	return res != 0 ? res : mir_strcmp(p1->value, p2->value);
}

// see RFC 3986 for details
#define isunreserved(c) ( isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_' || c == '~')
char *oauth_uri_escape(const char *str)
{
	int ix = 0;

	if (str == nullptr) return mir_strdup("");

	int size = (int)mir_strlen(str) + 1;
	char *res = (char *)mir_alloc(size);

	while (*str) {
		if (!isunreserved(*str)) {
			size += 2;
			res = (char *)mir_realloc(res, size);
			mir_snprintf(&res[ix], 4, "%%%X%X", (*str >> 4) & 15, *str & 15);
			ix += 3;
		}
		else
			res[ix++] = *str;
		str++;
	}
	res[ix] = 0;

	return res;
}

// generates Signature Base String

char *oauth_generate_signature(LIST<OAUTHPARAMETER> &params, const char *httpmethod, const char *url)
{
	char *res;
	OAUTHPARAMETER *p;
	int ix = 0;

	if (httpmethod == nullptr || url == nullptr || !params.getCount()) return mir_strdup("");

	char *urlnorm = (char *)mir_alloc(mir_strlen(url) + 1);
	while (*url) {
		if (*url == '?' || *url == '#')	break; // see RFC 3986 section 3
		urlnorm[ix++] = tolower(*url);
		url++;
	}
	urlnorm[ix] = 0;
	if ((res = strstr(urlnorm, ":80")) != nullptr)
		memmove(res, res + 3, mir_strlen(res) - 2);
	else if ((res = strstr(urlnorm, ":443")) != nullptr)
		memmove(res, res + 4, mir_strlen(res) - 3);

	char *urlenc = oauth_uri_escape(urlnorm);
	mir_free(urlnorm);
	int size = (int)mir_strlen(httpmethod) + (int)mir_strlen(urlenc) + 1 + 2;

	for (int i = 0; i < params.getCount(); i++) {
		p = params[i];
		if (!mir_strcmp(p->name, "oauth_signature")) continue;
		if (i > 0) size += 3;
		size += (int)mir_strlen(p->name) + (int)mir_strlen(p->value) + 3;
	}

	res = (char *)mir_alloc(size);
	mir_strcpy(res, httpmethod);
	mir_strcat(res, "&");
	mir_strcat(res, urlenc);
	mir_free(urlenc);
	mir_strcat(res, "&");

	for (int i = 0; i < params.getCount(); i++) {
		p = params[i];
		if (!mir_strcmp(p->name, "oauth_signature")) continue;
		if (i > 0) mir_strcat(res, "%26");
		mir_strcat(res, p->name);
		mir_strcat(res, "%3D");
		mir_strcat(res, p->value);
	}

	return res;
}

char *oauth_getparam(LIST<OAUTHPARAMETER> &params, const char *name)
{
	OAUTHPARAMETER *p;

	if (name == nullptr)
		return nullptr;

	for (int i = 0; i < params.getCount(); i++) {
		p = params[i];
		if (!mir_strcmp(p->name, name))
			return p->value;
	}

	return nullptr;
}

void oauth_setparam(LIST<OAUTHPARAMETER> &params, const char *name, const char *value)
{
	OAUTHPARAMETER *p;

	if (name == nullptr)
		return;

	for (int i = 0; i < params.getCount(); i++) {
		p = params[i];
		if (!mir_strcmp(p->name, name)) {
			mir_free(p->value);
			p->value = oauth_uri_escape(value);
			return;
		}
	}

	p = (OAUTHPARAMETER*)mir_alloc(sizeof(OAUTHPARAMETER));
	p->name = oauth_uri_escape(name);
	p->value = oauth_uri_escape(value);
	params.insert(p);
}

void oauth_freeparams(LIST<OAUTHPARAMETER> &params)
{
	OAUTHPARAMETER *p;

	for (int i = 0; i < params.getCount(); i++) {
		p = params[i];
		mir_free(p->name);
		mir_free(p->value);
	}
}

int oauth_sign_request(LIST<OAUTHPARAMETER> &params, const char *httpmethod, const char *url,
	const char *consumer_secret, const char *token_secret)
{
	char *sign = nullptr;

	if (!params.getCount())
		return -1;

	char *signmethod = oauth_getparam(params, "oauth_signature_method");
	if (signmethod == nullptr)
		return -1;

	if (!mir_strcmp(signmethod, "HMAC-SHA1")) {
		ptrA text(oauth_generate_signature(params, httpmethod, url));
		ptrA csenc(oauth_uri_escape(consumer_secret));
		ptrA tsenc(oauth_uri_escape(token_secret));
		ptrA key((char *)mir_alloc(mir_strlen(csenc) + mir_strlen(tsenc) + 2));
		mir_strcpy(key, csenc);
		mir_strcat(key, "&");
		mir_strcat(key, tsenc);

		BYTE digest[MIR_SHA1_HASH_SIZE];
		mir_hmac_sha1(digest, (BYTE*)(char*)key, mir_strlen(key), (BYTE*)(char*)text, mir_strlen(text));
		sign = mir_base64_encode(digest, MIR_SHA1_HASH_SIZE);
	}
	else { // PLAINTEXT
		ptrA csenc(oauth_uri_escape(consumer_secret));
		ptrA tsenc(oauth_uri_escape(token_secret));

		sign = (char *)mir_alloc(mir_strlen(csenc) + mir_strlen(tsenc) + 2);
		mir_strcpy(sign, csenc);
		mir_strcat(sign, "&");
		mir_strcat(sign, tsenc);
	}

	oauth_setparam(params, "oauth_signature", sign);
	mir_free(sign);

	return 0;
}

char* oauth_generate_nonce()
{
	char randnum[16];
	Utils_GetRandom(randnum, sizeof(randnum));

	CMStringA str(FORMAT, "%ld%s", time(nullptr), randnum);

	BYTE digest[16];
	mir_md5_hash((BYTE*)str.GetString(), str.GetLength(), digest);

	return bin2hex(digest, sizeof(digest), (char *)mir_alloc(32 + 1));
}

char *oauth_auth_header(const char *httpmethod, const char *url, OAUTHSIGNMETHOD signmethod,
	const char *consumer_key, const char *consumer_secret,
	const char *token, const char *token_secret)
{
	char *res, timestamp[22];

	if (httpmethod == nullptr || url == nullptr)
		return nullptr;

	LIST<OAUTHPARAMETER> oauth_parameters(1, paramsortFunc);
	oauth_setparam(oauth_parameters, "oauth_consumer_key", consumer_key);
	oauth_setparam(oauth_parameters, "oauth_version", "1.0");
	switch (signmethod) {
	case HMACSHA1:
		oauth_setparam(oauth_parameters, "oauth_signature_method", "HMAC-SHA1");
		break;

	case RSASHA1:
		oauth_setparam(oauth_parameters, "oauth_signature_method", "RSA-SHA1");
		break;

	default:
		oauth_setparam(oauth_parameters, "oauth_signature_method", "PLAINTEXT");
		break;
	};
	mir_snprintf(timestamp, "%ld", time(nullptr));
	oauth_setparam(oauth_parameters, "oauth_timestamp", timestamp);
	oauth_setparam(oauth_parameters, "oauth_nonce", ptrA(oauth_generate_nonce()));
	if (token != nullptr && *token)
		oauth_setparam(oauth_parameters, "oauth_token", token);

	if (oauth_sign_request(oauth_parameters, httpmethod, url, consumer_secret, token_secret)) {
		oauth_freeparams(oauth_parameters);
		return nullptr;
	}

	int size = 7;
	for (int i = 0; i < oauth_parameters.getCount(); i++) {
		OAUTHPARAMETER *p = oauth_parameters[i];
		if (i > 0) size++;
		size += (int)mir_strlen(p->name) + (int)mir_strlen(p->value) + 3;
	}

	res = (char *)mir_alloc(size);
	mir_strcpy(res, "OAuth ");

	for (int i = 0; i < oauth_parameters.getCount(); i++) {
		OAUTHPARAMETER *p = oauth_parameters[i];
		if (i > 0) mir_strcat(res, ",");
		mir_strcat(res, p->name);
		mir_strcat(res, "=\"");
		mir_strcat(res, p->value);
		mir_strcat(res, "\"");
	}

	oauth_freeparams(oauth_parameters);
	return res;
}

int GGPROTO::oauth_receivetoken()
{
	char szUrl[256], uin[32], *token = nullptr, *token_secret = nullptr;
	int res = 0;
	HNETLIBCONN nlc = nullptr;

	UIN2IDA(getDword(GG_KEY_UIN, 0), uin);
	char *password = getStringA(GG_KEY_PASSWORD);

	// 1. Obtaining an Unauthorized Request Token
	debugLogA("oauth_receivetoken(): Obtaining an Unauthorized Request Token...");
	mir_strcpy(szUrl, "http://api.gadu-gadu.pl/request_token");
	char *str = oauth_auth_header("POST", szUrl, HMACSHA1, uin, password, nullptr, nullptr);

	NETLIBHTTPHEADER httpHeaders[3];
	httpHeaders[0].szName = "User-Agent";
	httpHeaders[0].szValue = GG8_VERSION;
	httpHeaders[1].szName = "Authorization";
	httpHeaders[1].szValue = str;
	httpHeaders[2].szName = "Accept";
	httpHeaders[2].szValue = "*/*";

	NETLIBHTTPREQUEST req = { sizeof(req) };
	req.requestType = REQUEST_POST;
	req.szUrl = szUrl;
	req.flags = NLHRF_NODUMP | NLHRF_HTTP11 | NLHRF_PERSISTENT;
	req.headersCount = 3;
	req.headers = httpHeaders;

	NETLIBHTTPREQUEST *resp = Netlib_HttpTransaction(m_hNetlibUser, &req);
	if (resp) {
		nlc = resp->nlc;
		if (resp->resultCode == 200 && resp->dataLength > 0 && resp->pData) {
			wchar_t *xmlAction = mir_a2u(resp->pData);
			HXML hXml = xmlParseString(xmlAction, nullptr, L"result");
			if (hXml != nullptr) {
				HXML node = xmlGetChildByPath(hXml, L"oauth_token", 0);
				token = node != nullptr ? mir_u2a(xmlGetText(node)) : nullptr;

				node = xmlGetChildByPath(hXml, L"oauth_token_secret", 0);
				token_secret = node != nullptr ? mir_u2a(xmlGetText(node)) : nullptr;

				xmlDestroyNode(hXml);
			}
			mir_free(xmlAction);
		}
		else
			debugLogA("oauth_receivetoken(): Invalid response code from HTTP request");

		Netlib_FreeHttpRequest(resp);
	}
	else
		debugLogA("oauth_receivetoken(): No response from HTTP request");

	// 2. Obtaining User Authorization
	debugLogA("oauth_receivetoken(): Obtaining User Authorization...");
	mir_free(str);
	str = oauth_uri_escape("http://www.mojageneracja.pl");

	mir_snprintf(szUrl, "callback_url=%s&request_token=%s&uin=%s&password=%s",
		str, token, uin, password);
	mir_free(str);
	str = mir_strdup(szUrl);

	memset(&req, 0, sizeof(req));
	req.cbSize = sizeof(req);
	req.requestType = REQUEST_POST;
	req.szUrl = szUrl;
	req.flags = NLHRF_NODUMP | NLHRF_HTTP11;
	req.headersCount = 3;
	req.headers = httpHeaders;
	mir_strcpy(szUrl, "https://login.gadu-gadu.pl/authorize");
	httpHeaders[1].szName = "Content-Type";
	httpHeaders[1].szValue = "application/x-www-form-urlencoded";
	req.pData = str;
	req.dataLength = (int)mir_strlen(str);

	resp = Netlib_HttpTransaction(m_hNetlibUser, &req);
	if (resp)
		Netlib_FreeHttpRequest(resp);
	else
		debugLogA("oauth_receivetoken(): No response from HTTP request");

	// 3. Obtaining an Access Token
	debugLogA("oauth_receivetoken(): Obtaining an Access Token...");
	mir_strcpy(szUrl, "http://api.gadu-gadu.pl/access_token");
	mir_free(str);
	str = oauth_auth_header("POST", szUrl, HMACSHA1, uin, password, token, token_secret);
	mir_free(token);
	mir_free(token_secret);
	token = nullptr;
	token_secret = nullptr;

	memset(&req, 0, sizeof(req));
	req.cbSize = sizeof(req);
	req.requestType = REQUEST_POST;
	req.szUrl = szUrl;
	req.flags = NLHRF_NODUMP | NLHRF_HTTP11 | NLHRF_PERSISTENT;
	req.nlc = nlc;
	req.headersCount = 3;
	req.headers = httpHeaders;
	httpHeaders[1].szName = "Authorization";
	httpHeaders[1].szValue = str;

	resp = Netlib_HttpTransaction(m_hNetlibUser, &req);
	if (resp) {
		if (resp->resultCode == 200 && resp->dataLength > 0 && resp->pData) {
			wchar_t *xmlAction = mir_a2u(resp->pData);
			HXML hXml = xmlParseString(xmlAction, nullptr, L"result");
			if (hXml != nullptr) {
				HXML node = xmlGetChildByPath(hXml, L"oauth_token", 0);
				token = mir_u2a(xmlGetText(node));

				node = xmlGetChildByPath(hXml, L"oauth_token_secret", 0);
				token_secret = mir_u2a(xmlGetText(node));

				xmlDestroyNode(hXml);
			}
			mir_free(xmlAction);
		}
		else
			debugLogA("oauth_receivetoken(): Invalid response code from HTTP request");

		Netlib_CloseHandle(resp->nlc);
		Netlib_FreeHttpRequest(resp);
	}
	else
		debugLogA("oauth_receivetoken(): No response from HTTP request");

	mir_free(password);
	mir_free(str);

	if (token != nullptr && token_secret != nullptr) {
		setString(GG_KEY_TOKEN, token);
		setString(GG_KEY_TOKENSECRET, token_secret);
		debugLogA("oauth_receivetoken(): Access Token obtained successfully.");
		res = 1;
	}
	else {
		delSetting(GG_KEY_TOKEN);
		delSetting(GG_KEY_TOKENSECRET);
		debugLogA("oauth_receivetoken(): Failed to obtain Access Token.");
	}
	mir_free(token);
	mir_free(token_secret);

	return res;
}

int GGPROTO::oauth_checktoken(int force)
{
	if (force)
		return oauth_receivetoken();

	ptrA token(getStringA(GG_KEY_TOKEN));
	ptrA token_secret(getStringA(GG_KEY_TOKENSECRET));
	if (token == NULL || token_secret == NULL)
		return oauth_receivetoken();

	return 1;
}
