// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include "key.h"

// Generate a private key from just the secret parameter
int EC_KEY_regenerate_key(EC_KEY *eckey, BIGNUM *priv_key)
{
    int ok = 0;
    BN_CTX *ctx = NULL;
    EC_POINT *pub_key = NULL;

    if (!eckey) return 0;

    const EC_GROUP *group = EC_KEY_get0_group(eckey);

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;

    pub_key = EC_POINT_new(group);

    if (pub_key == NULL)
        goto err;

    if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
        goto err;

    EC_KEY_set_private_key(eckey,priv_key);
    EC_KEY_set_public_key(eckey,pub_key);

    ok = 1;

err:

    if (pub_key)
        EC_POINT_free(pub_key);
    if (ctx != NULL)
        BN_CTX_free(ctx);

    return(ok);
}

// Perform ECDSA key recovery (see SEC1 4.1.6) for curves over (mod p)-fields
// recid selects which key is recovered
// if check is non-zero, additional checks are performed
int ECDSA_SIG_recover_key_GFp(EC_KEY *eckey, ECDSA_SIG *ecsig, const unsigned char *msg, int msglen, int recid, int check)
{
    if (!eckey) return 0;

    int ret = 0;
    BN_CTX *ctx = NULL;

    BIGNUM *x = NULL;
    BIGNUM *e = NULL;
    BIGNUM *order = NULL;
    BIGNUM *sor = NULL;
    BIGNUM *eor = NULL;
    BIGNUM *field = NULL;
    EC_POINT *R = NULL;
    EC_POINT *O = NULL;
    EC_POINT *Q = NULL;
    BIGNUM *rr = NULL;
    BIGNUM *zero = NULL;
    int n = 0;
    int i = recid / 2;

    const EC_GROUP *group = EC_KEY_get0_group(eckey);
    if ((ctx = BN_CTX_new()) == NULL) { ret = -1; goto err; }
    BN_CTX_start(ctx);
    order = BN_CTX_get(ctx);
    if (!EC_GROUP_get_order(group, order, ctx)) { ret = -2; goto err; }
    x = BN_CTX_get(ctx);
    if (!BN_copy(x, order)) { ret=-1; goto err; }
    if (!BN_mul_word(x, i)) { ret=-1; goto err; }
    if (!BN_add(x, x, ecsig->r)) { ret=-1; goto err; }
    field = BN_CTX_get(ctx);
    if (!EC_GROUP_get_curve_GFp(group, field, NULL, NULL, ctx)) { ret=-2; goto err; }
    if (BN_cmp(x, field) >= 0) { ret=0; goto err; }
    if ((R = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
    if (!EC_POINT_set_compressed_coordinates_GFp(group, R, x, recid % 2, ctx)) { ret=0; goto err; }
    if (check)
    {
        if ((O = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
        if (!EC_POINT_mul(group, O, NULL, R, order, ctx)) { ret=-2; goto err; }
        if (!EC_POINT_is_at_infinity(group, O)) { ret = 0; goto err; }
    }
    if ((Q = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
    n = EC_GROUP_get_degree(group);
    e = BN_CTX_get(ctx);
    if (!BN_bin2bn(msg, msglen, e)) { ret=-1; goto err; }
    if (8*msglen > n) BN_rshift(e, e, 8-(n & 7));
    zero = BN_CTX_get(ctx);
    if (!BN_zero(zero)) { ret=-1; goto err; }
    if (!BN_mod_sub(e, zero, e, order, ctx)) { ret=-1; goto err; }
    rr = BN_CTX_get(ctx);
    if (!BN_mod_inverse(rr, ecsig->r, order, ctx)) { ret=-1; goto err; }
    sor = BN_CTX_get(ctx);
    if (!BN_mod_mul(sor, ecsig->s, rr, order, ctx)) { ret=-1; goto err; }
    eor = BN_CTX_get(ctx);
    if (!BN_mod_mul(eor, e, rr, order, ctx)) { ret=-1; goto err; }
    if (!EC_POINT_mul(group, Q, eor, R, sor, ctx)) { ret=-2; goto err; }
    if (!EC_KEY_set_public_key(eckey, Q)) { ret=-2; goto err; }

    ret = 1;

err:
    if (ctx) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
    }
    if (R != NULL) EC_POINT_free(R);
    if (O != NULL) EC_POINT_free(O);
    if (Q != NULL) EC_POINT_free(Q);
    return ret;
}

void CKey::SetCompressedPubKey()
{
    EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
    fCompressedPubKey = true;
}

void CKey::Reset()
{
    fCompressedPubKey = false;
    if (pkey != NULL)
        EC_KEY_free(pkey);
    pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (pkey == NULL)
        throw key_error("CKey::CKey() : EC_KEY_new_by_curve_name failed");
    fSet = false;
}

CKey::CKey()
{
    pkey = NULL;
    Reset();
}

CKey::CKey(const CKey& b)
{
    pkey = EC_KEY_dup(b.pkey);
    if (pkey == NULL)
        throw key_error("CKey::CKey(const CKey&) : EC_KEY_dup failed");
    fSet = b.fSet;
}

CKey& CKey::operator=(const CKey& b)
{
    if (!EC_KEY_copy(pkey, b.pkey))
        throw key_error("CKey::operator=(const CKey&) : EC_KEY_copy failed");
    fSet = b.fSet;
    return (*this);
}

CKey::~CKey()
{
    EC_KEY_free(pkey);
}

bool CKey::IsNull() const
{
    return !fSet;
}

bool CKey::IsCompressed() const
{
    return fCompressedPubKey;
}

void CKey::MakeNewKey(bool fCompressed)
{
    if (!EC_KEY_generate_key(pkey))
        throw key_error("CKey::MakeNewKey() : EC_KEY_generate_key failed");
    if (fCompressed)
        SetCompressedPubKey();
    fSet = true;
}

bool CKey::SetPrivKey(const CPrivKey& vchPrivKey)
{
    const unsigned char* pbegin = &vchPrivKey[0];
    if (d2i_ECPrivateKey(&pkey, &pbegin, vchPrivKey.size()))
    {
        // In testing, d2i_ECPrivateKey can return true
        // but fill in pkey with a key that fails
        // EC_KEY_check_key, so:
        if (EC_KEY_check_key(pkey))
        {
            fSet = true;
            return true;
        }
    }
    // If vchPrivKey data is bad d2i_ECPrivateKey() can
    // leave pkey in a state where calling EC_KEY_free()
    // crashes. To avoid that, set pkey to NULL and
    // leak the memory (a leak is better than a crash)
    pkey = NULL;
    Reset();
    return false;
}

bool CKey::SetSecret(const CSecret& vchSecret, bool fCompressed)
{
    EC_KEY_free(pkey);
    pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (pkey == NULL)
        throw key_error("CKey::SetSecret() : EC_KEY_new_by_curve_name failed");
    if (vchSecret.size() != 32)
        throw key_error("CKey::SetSecret() : secret must be 32 bytes");
    BIGNUM *bn = BN_bin2bn(&vchSecret[0],32,BN_new());
    if (bn == NULL)
        throw key_error("CKey::SetSecret() : BN_bin2bn failed");
    if (!EC_KEY_regenerate_key(pkey,bn))
    {
        BN_clear_free(bn);
        throw key_error("CKey::SetSecret() : EC_KEY_regenerate_key failed");
    }
    BN_clear_free(bn);
    fSet = true;
    if (fCompressed || fCompressedPubKey)
        SetCompressedPubKey();
    return true;
}

CSecret CKey::GetSecret(bool &fCompressed) const
{
    CSecret vchRet;
    vchRet.resize(32);
    const BIGNUM *bn = EC_KEY_get0_private_key(pkey);
    int nBytes = BN_num_bytes(bn);
    if (bn == NULL)
        throw key_error("CKey::GetSecret() : EC_KEY_get0_private_key failed");
    int n=BN_bn2bin(bn,&vchRet[32 - nBytes]);
    if (n != nBytes)
        throw key_error("CKey::GetSecret(): BN_bn2bin failed");
    fCompressed = fCompressedPubKey;
    return vchRet;
}

CPrivKey CKey::GetPrivKey() const
{
    int nSize = i2d_ECPrivateKey(pkey, NULL);
    if (!nSize)
        throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey failed");
    CPrivKey vchPrivKey(nSize, 0);
    unsigned char* pbegin = &vchPrivKey[0];
    if (i2d_ECPrivateKey(pkey, &pbegin) != nSize)
        throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey returned unexpected size");
    return vchPrivKey;
}

bool CKey::SetPubKey(const CPubKey& vchPubKey)
{
    const unsigned char* pbegin = &vchPubKey.vchPubKey[0];
    if (o2i_ECPublicKey(&pkey, &pbegin, vchPubKey.vchPubKey.size()))
    {
        fSet = true;
        if (vchPubKey.vchPubKey.size() == 33)
            SetCompressedPubKey();
        return true;
    }
    pkey = NULL;
    Reset();
    return false;
}

CPubKey CKey::GetPubKey() const
{
    int nSize = i2o_ECPublicKey(pkey, NULL);
    if (!nSize)
        throw key_error("CKey::GetPubKey() : i2o_ECPublicKey failed");
    std::vector<unsigned char> vchPubKey(nSize, 0);
    unsigned char* pbegin = &vchPubKey[0];
    if (i2o_ECPublicKey(pkey, &pbegin) != nSize)
        throw key_error("CKey::GetPubKey() : i2o_ECPublicKey returned unexpected size");
    return CPubKey(vchPubKey);
}

bool CKey::Sign(uint256 hash, std::vector<unsigned char>& vchSig)
{
    unsigned int nSize = ECDSA_size(pkey);
    vchSig.resize(nSize); // Make sure it is big enough
    if (!ECDSA_sign(0, (unsigned char*)&hash, sizeof(hash), &vchSig[0], &nSize, pkey))
    {
        vchSig.clear();
        return false;
    }
    vchSig.resize(nSize); // Shrink to fit actual size
    return true;
}

// create a compact signature (65 bytes), which allows reconstructing the used public key
// The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
// The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
//                  0x1D = second key with even y, 0x1E = second key with odd y
bool CKey::SignCompact(uint256 hash, std::vector<unsigned char>& vchSig)
{
    bool fOk = false;
    ECDSA_SIG *sig = ECDSA_do_sign((unsigned char*)&hash, sizeof(hash), pkey);
    if (sig==NULL)
        return false;
    vchSig.clear();
    vchSig.resize(65,0);
    int nBitsR = BN_num_bits(sig->r);
    int nBitsS = BN_num_bits(sig->s);
    if (nBitsR <= 256 && nBitsS <= 256)
    {
        int nRecId = -1;
        for (int i=0; i<4; i++)
        {
            CKey keyRec;
            keyRec.fSet = true;
            if (fCompressedPubKey)
                keyRec.SetCompressedPubKey();
            if (ECDSA_SIG_recover_key_GFp(keyRec.pkey, sig, (unsigned char*)&hash, sizeof(hash), i, 1) == 1)
                if (keyRec.GetPubKey() == this->GetPubKey())
                {
                    nRecId = i;
                    break;
                }
        }

        if (nRecId == -1)
        {
            ECDSA_SIG_free(sig);
            throw key_error("CKey::SignCompact() : unable to construct recoverable key");
        }

        vchSig[0] = nRecId+27+(fCompressedPubKey ? 4 : 0);
        BN_bn2bin(sig->r,&vchSig[33-(nBitsR+7)/8]);
        BN_bn2bin(sig->s,&vchSig[65-(nBitsS+7)/8]);
        fOk = true;
    }
    ECDSA_SIG_free(sig);
    return fOk;
}

// reconstruct public key from a compact signature
// This is only slightly more CPU intensive than just verifying it.
// If this function succeeds, the recovered public key is guaranteed to be valid
// (the signature is a valid signature of the given data for that key)
bool CKey::SetCompactSignature(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    if (vchSig.size() != 65)
        return false;
    int nV = vchSig[0];
    if (nV<27 || nV>=35)
        return false;
    ECDSA_SIG *sig = ECDSA_SIG_new();
    BN_bin2bn(&vchSig[1],32,sig->r);
    BN_bin2bn(&vchSig[33],32,sig->s);

    EC_KEY_free(pkey);
    pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (nV >= 31)
    {
        SetCompressedPubKey();
        nV -= 4;
    }
    if (ECDSA_SIG_recover_key_GFp(pkey, sig, (unsigned char*)&hash, sizeof(hash), nV - 27, 0) == 1)
    {
        fSet = true;
        ECDSA_SIG_free(sig);
        return true;
    }
    ECDSA_SIG_free(sig);
    return false;
}

bool CKey::Verify(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    // -1 = error, 0 = bad sig, 1 = good
    if (ECDSA_verify(0, (unsigned char*)&hash, sizeof(hash), &vchSig[0], vchSig.size(), pkey) != 1)
        return false;

    return true;
}

bool CKey::VerifyCompact(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    CKey key;
    if (!key.SetCompactSignature(hash, vchSig))
        return false;
    if (GetPubKey() != key.GetPubKey())
        return false;

    return true;
}

bool CKey::IsValid()
{
    if (!fSet)
        return false;

    if (!EC_KEY_check_key(pkey))
        return false;

    bool fCompr;
    CSecret secret = GetSecret(fCompr);
    CKey key2;
    key2.SetSecret(secret, fCompr);
    return GetPubKey() == key2.GetPubKey();
}

CPoint::CPoint()
{
    std::string err;
    group = NULL;
    point = NULL;
    ctx   = NULL;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        err = "EC_KEY_new_by_curve_name failed.";
        goto finish;
    }

    point = EC_POINT_new(group);
    if (!point) {
        err = "EC_POINT_new failed.";
        goto finish;
    }

    ctx = BN_CTX_new();
    if (!ctx) {
        err = "BN_CTX_new failed.";
        goto finish;
    }

    return;

finish:
    if (group) EC_GROUP_free(group);
    if (point) EC_POINT_free(point);
    throw std::runtime_error(std::string("CPoint::CPoint() :  - ") + err);
}

bool CPoint::operator!=(const CPoint &a)
{
    if (EC_POINT_cmp(group, point, a.point, ctx) != 0)
        return true;
    return false;
}
CPoint::~CPoint()
{
    if (point) EC_POINT_free(point);
    if (group) EC_GROUP_free(group);
    if (ctx)   BN_CTX_free(ctx);
}

// Initialize from octets stream
bool CPoint::setBytes(const std::vector<unsigned char> &vchBytes)
{
    if (!EC_POINT_oct2point(group, point, &vchBytes[0], vchBytes.size(), ctx)) {
        return false;
    }
    return true;
}

// Serialize to octets stream
bool CPoint::getBytes(std::vector<unsigned char> &vchBytes)
{
    unsigned int nSize = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, NULL, 0, ctx);
    vchBytes.resize(nSize);
    if (!(nSize == EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, &vchBytes[0], nSize, ctx))) {
        return false;
    }
    return true;
}

// ECC multiplication by specified multiplier
bool CPoint::ECMUL(const CBigNum &bnMultiplier)
{
    if (!EC_POINT_mul(group, point, NULL, point, &bnMultiplier, NULL)) {
        printf("CPoint::ECMUL() : EC_POINT_mul failed");
        return false;
    }

    return true;
}

// Calculate G*m + q
bool CPoint::ECMULGEN(const CBigNum &bnMultiplier, const CPoint &qPoint)
{
    if (!EC_POINT_mul(group, point, &bnMultiplier, qPoint.point, BN_value_one(), NULL)) {
        printf("CPoint::ECMULGEN() : EC_POINT_mul failed.");
        return false;
    }

    return true;
}

// CMutablePubKey

bool CMutablePubKey::GetVariant(CPubKey &R, CPubKey &vchPubKeyVariant)
{
    EC_KEY *eckey = NULL;
    eckey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (eckey == NULL) {
        throw key_error("CMutablePubKey::GetVariant() : EC_KEY_new_by_curve_name failed");
    }

    // Use standard key generation function to get r and R values.
    //
    // r will be presented by private key;
    // R is ECDSA public key which calculated as G*r
    if (!EC_KEY_generate_key(eckey)) {
        throw key_error("CMutablePubKey::GetVariant() : EC_KEY_generate_key failed");
    }

    EC_KEY_set_conv_form(eckey, POINT_CONVERSION_COMPRESSED);

    int nSize = i2o_ECPublicKey(eckey, NULL);
    if (!nSize) {
        throw key_error("CMutablePubKey::GetVariant() : i2o_ECPublicKey failed");
    }

    std::vector<unsigned char> vchPubKey(nSize, 0);
    unsigned char* pbegin_R = &vchPubKey[0];

    if (i2o_ECPublicKey(eckey, &pbegin_R) != nSize) {
        throw key_error("CMutablePubKey::GetVariant() : i2o_ECPublicKey returned unexpected size");
    }

    // R = G*r
    R = CPubKey(vchPubKey);

    // OpenSSL BIGNUM representation of r value
    CBigNum bnr;
    bnr = *(CBigNum*) EC_KEY_get0_private_key(eckey);
    EC_KEY_free(eckey);

    CPoint point;
    if (!point.setBytes(pubKeyL.Raw())) {
        throw key_error("CMutablePubKey::GetVariant() : Unable to decode L value");
    }

    // Calculate L*r
    point.ECMUL(bnr);

    std::vector<unsigned char> vchLr;
    if (!point.getBytes(vchLr)) {
        throw key_error("CMutablePubKey::GetVariant() : Unable to convert Lr value");
    }

    // Calculate Hash(L*r) and then get a BIGNUM representation of hash value.
    CBigNum bnHash;
    bnHash.setuint160(Hash160(vchLr));

    CPoint pointH;
    pointH.setBytes(pubKeyH.Raw());

    CPoint P;
    // Calculate P = Hash(L*r)*G + H
    P.ECMULGEN(bnHash, pointH);

    if (P.IsInfinity())
        return false;

    std::vector<unsigned char> vchResult;
    P.getBytes(vchResult);

    vchPubKeyVariant = CPubKey(vchResult);

    return true;
}

// CMutableKey

void CMutableKey::Reset()
{
    keyL.Reset();
    keyH.Reset();
}

CMutableKey::CMutableKey()
{
    keyL = CKey();
    keyH = CKey();
    Reset();
}

CMutableKey::CMutableKey(const CMutableKey &b)
{
    keyL = CKey(b.keyL);
    keyH = CKey(b.keyH);
}

CMutableKey& CMutableKey::operator=(const CMutableKey &b)
{
    keyL = b.keyL;
    keyH = b.keyH;

    return (*this);
}

CMutableKey::~CMutableKey()
{
}

bool CMutableKey::IsNull() const
{
    return keyL.IsNull() || keyH.IsNull();
}

void CMutableKey::MakeNewKeys()
{
    keyL.MakeNewKey(true);
    keyH.MakeNewKey(true);
}


bool CMutableKey::SetPrivKeys(const CPrivKey &vchPrivKeyL, const CPrivKey &vchPrivKeyH)
{
    if (!keyL.SetPrivKey(vchPrivKeyL) || !keyH.SetPrivKey(vchPrivKeyH))
        return false;

    return true;
}

bool CMutableKey::SetSecrets(const CSecret &vchSecretL, const CSecret &vchSecretH)
{
    if (!keyL.SetSecret(vchSecretL, true) || !keyH.SetSecret(vchSecretH, true))
        return false;

    return true;
}

void CMutableKey::GetSecrets(CSecret &vchSecretL, CSecret &vchSecretH) const
{
    bool fCompressed = true;

    vchSecretL = keyL.GetSecret(fCompressed);
    vchSecretH = keyH.GetSecret(fCompressed);
}

void CMutableKey::GetPrivKeys(CPrivKey &vchPrivKeyL, CPrivKey &vchPrivKeyH) const
{
    vchPrivKeyL = keyL.GetPrivKey();
    vchPrivKeyH = keyH.GetPrivKey();
}

CMutablePubKey CMutableKey::GetMutablePubKey() const
{
    return CMutablePubKey(keyL.GetPubKey(), keyH.GetPubKey());
}

bool CMutableKey::CheckKeyVariant(const CPubKey &R, const CPubKey &H, const CPubKey &vchPubKeyVariant, CKey &privKeyVariant)
{
    if (!R.IsValid())
    {
        printf("CMutableKey::CheckKeyVariant() : R is invalid");
        return false;
    }

    if (!H.IsValid())
    {
        printf("CMutableKey::CheckKeyVariant() : H is invalid");
        return false;
    }

    if (!vchPubKeyVariant.IsValid())
    {
        printf("CMutableKey::CheckKeyVariant() : public key variant is invalid");
        return false;
    }

    CPoint point_R;
    if (!point_R.setBytes(R.Raw())) {
        printf("CMutableKey::CheckKeyVariant() : Unable to decode R value");
        return false;
    }

    CPoint point_H;
    if (!point_H.setBytes(H.Raw())) {
        printf("CMutableKey::CheckKeyVariant() : Unable to decode H value");
        return false;
    }

    CPoint point_P;
    if (!point_P.setBytes(vchPubKeyVariant.Raw())) {
        printf("CMutableKey::CheckKeyVariant() : Unable to decode P value");
        return false;
    }

    // Infinity points are senseless
    if (point_P.IsInfinity())
        return false;

    bool fCompressed;
    CSecret vchSecretL = keyL.GetSecret(fCompressed);
    CSecret vchSecretH = keyH.GetSecret(fCompressed);

    CBigNum bnl;
    bnl.setBytes(std::vector<unsigned char>(vchSecretL.begin(), vchSecretL.end()));

    point_R.ECMUL(bnl);

    std::vector<unsigned char> vchRl;
    if (!point_R.getBytes(vchRl)) {
        printf("CMutableKey::CheckKeyVariant() : Unable to convert Rl value");
        return false;
    }

    // Calculate Hash(R*l)
    CBigNum bnHash;
    bnHash.setuint160(Hash160(vchRl));

    CPoint point_Ps;
    // Calculate Ps = Hash(L*r)*G + H
    point_Ps.ECMULGEN(bnHash, point_H);

    // Infinity points are senseless
    if (point_Ps.IsInfinity()) {
        return false;
    }

    // Check ownership
    if (point_Ps.IsInfinity() || point_Ps != point_P) {
        return false;
    }

    // OpenSSL BIGNUM representation of the second private key from (l, h) pair
    CBigNum bnh;
    bnh.setBytes(std::vector<unsigned char>(vchSecretH.begin(), vchSecretH.end()));

    // Calculate p = Hash(R*l) + h
    CBigNum bnp = bnHash + bnh;

    std::vector<unsigned char> vchp = bnp.getBytes();
    privKeyVariant.SetSecret(CSecret(vchp.begin(), vchp.end()), true);

    return true;
}
