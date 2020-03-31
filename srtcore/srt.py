# This file was automatically generated by SWIG (http://www.swig.org).
# Version 4.0.1
#
# Do not make changes to this file unless you know what you are doing--modify
# the SWIG interface file instead.

from sys import version_info as _swig_python_version_info
if _swig_python_version_info < (2, 7, 0):
    raise RuntimeError("Python 2.7 or later required")

# Import the low-level C/C++ module
if __package__ or "." in __name__:
    from . import _srt
else:
    import _srt

try:
    import builtins as __builtin__
except ImportError:
    import __builtin__

def _swig_repr(self):
    try:
        strthis = "proxy of " + self.this.__repr__()
    except __builtin__.Exception:
        strthis = ""
    return "<%s.%s; %s >" % (self.__class__.__module__, self.__class__.__name__, strthis,)


def _swig_setattr_nondynamic_instance_variable(set):
    def set_instance_attr(self, name, value):
        if name == "thisown":
            self.this.own(value)
        elif name == "this":
            set(self, name, value)
        elif hasattr(self, name) and isinstance(getattr(type(self), name), property):
            set(self, name, value)
        else:
            raise AttributeError("You cannot add instance attributes to %s" % self)
    return set_instance_attr


def _swig_setattr_nondynamic_class_variable(set):
    def set_class_attr(cls, name, value):
        if hasattr(cls, name) and not isinstance(getattr(cls, name), property):
            set(cls, name, value)
        else:
            raise AttributeError("You cannot add class attributes to %s" % cls)
    return set_class_attr


def _swig_add_metaclass(metaclass):
    """Class decorator for adding a metaclass to a SWIG wrapped class - a slimmed down version of six.add_metaclass"""
    def wrapper(cls):
        return metaclass(cls.__name__, cls.__bases__, cls.__dict__.copy())
    return wrapper


class _SwigNonDynamicMeta(type):
    """Meta class to enforce nondynamic attributes (no new attributes) for a class"""
    __setattr__ = _swig_setattr_nondynamic_class_variable(type.__setattr__)


SRT_VERSION_FEAT_HSv5 = _srt.SRT_VERSION_FEAT_HSv5
SRTS_INIT = _srt.SRTS_INIT
SRTS_OPENED = _srt.SRTS_OPENED
SRTS_LISTENING = _srt.SRTS_LISTENING
SRTS_CONNECTING = _srt.SRTS_CONNECTING
SRTS_CONNECTED = _srt.SRTS_CONNECTED
SRTS_BROKEN = _srt.SRTS_BROKEN
SRTS_CLOSING = _srt.SRTS_CLOSING
SRTS_CLOSED = _srt.SRTS_CLOSED
SRTS_NONEXIST = _srt.SRTS_NONEXIST
SRTO_MSS = _srt.SRTO_MSS
SRTO_SNDSYN = _srt.SRTO_SNDSYN
SRTO_RCVSYN = _srt.SRTO_RCVSYN
SRTO_ISN = _srt.SRTO_ISN
SRTO_FC = _srt.SRTO_FC
SRTO_SNDBUF = _srt.SRTO_SNDBUF
SRTO_RCVBUF = _srt.SRTO_RCVBUF
SRTO_LINGER = _srt.SRTO_LINGER
SRTO_UDP_SNDBUF = _srt.SRTO_UDP_SNDBUF
SRTO_UDP_RCVBUF = _srt.SRTO_UDP_RCVBUF
SRTO_RENDEZVOUS = _srt.SRTO_RENDEZVOUS
SRTO_SNDTIMEO = _srt.SRTO_SNDTIMEO
SRTO_RCVTIMEO = _srt.SRTO_RCVTIMEO
SRTO_REUSEADDR = _srt.SRTO_REUSEADDR
SRTO_MAXBW = _srt.SRTO_MAXBW
SRTO_STATE = _srt.SRTO_STATE
SRTO_EVENT = _srt.SRTO_EVENT
SRTO_SNDDATA = _srt.SRTO_SNDDATA
SRTO_RCVDATA = _srt.SRTO_RCVDATA
SRTO_SENDER = _srt.SRTO_SENDER
SRTO_TSBPDMODE = _srt.SRTO_TSBPDMODE
SRTO_LATENCY = _srt.SRTO_LATENCY
SRTO_TSBPDDELAY = _srt.SRTO_TSBPDDELAY
SRTO_INPUTBW = _srt.SRTO_INPUTBW
SRTO_OHEADBW = _srt.SRTO_OHEADBW
SRTO_PASSPHRASE = _srt.SRTO_PASSPHRASE
SRTO_PBKEYLEN = _srt.SRTO_PBKEYLEN
SRTO_KMSTATE = _srt.SRTO_KMSTATE
SRTO_IPTTL = _srt.SRTO_IPTTL
SRTO_IPTOS = _srt.SRTO_IPTOS
SRTO_TLPKTDROP = _srt.SRTO_TLPKTDROP
SRTO_SNDDROPDELAY = _srt.SRTO_SNDDROPDELAY
SRTO_NAKREPORT = _srt.SRTO_NAKREPORT
SRTO_VERSION = _srt.SRTO_VERSION
SRTO_PEERVERSION = _srt.SRTO_PEERVERSION
SRTO_CONNTIMEO = _srt.SRTO_CONNTIMEO
_DEPRECATED_SRTO_SNDPBKEYLEN = _srt._DEPRECATED_SRTO_SNDPBKEYLEN
SRTO_SNDKMSTATE = _srt.SRTO_SNDKMSTATE
SRTO_RCVKMSTATE = _srt.SRTO_RCVKMSTATE
SRTO_LOSSMAXTTL = _srt.SRTO_LOSSMAXTTL
SRTO_RCVLATENCY = _srt.SRTO_RCVLATENCY
SRTO_PEERLATENCY = _srt.SRTO_PEERLATENCY
SRTO_MINVERSION = _srt.SRTO_MINVERSION
SRTO_STREAMID = _srt.SRTO_STREAMID
SRTO_CONGESTION = _srt.SRTO_CONGESTION
SRTO_MESSAGEAPI = _srt.SRTO_MESSAGEAPI
SRTO_PAYLOADSIZE = _srt.SRTO_PAYLOADSIZE
SRTO_TRANSTYPE = _srt.SRTO_TRANSTYPE
SRTO_KMREFRESHRATE = _srt.SRTO_KMREFRESHRATE
SRTO_KMPREANNOUNCE = _srt.SRTO_KMPREANNOUNCE
SRTO_ENFORCEDENCRYPTION = _srt.SRTO_ENFORCEDENCRYPTION
SRTO_IPV6ONLY = _srt.SRTO_IPV6ONLY
SRTO_PEERIDLETIMEO = _srt.SRTO_PEERIDLETIMEO
SRTO_GROUPCONNECT = _srt.SRTO_GROUPCONNECT
SRTO_GROUPSTABTIMEO = _srt.SRTO_GROUPSTABTIMEO
SRTO_PACKETFILTER = _srt.SRTO_PACKETFILTER
SRTO_DEPRECATED_END = _srt.SRTO_DEPRECATED_END
SRTT_LIVE = _srt.SRTT_LIVE
SRTT_FILE = _srt.SRTT_FILE
SRTT_INVALID = _srt.SRTT_INVALID
class CBytePerfMon(object):
    thisown = property(lambda x: x.this.own(), lambda x, v: x.this.own(v), doc="The membership flag")
    __repr__ = _swig_repr
    msTimeStamp = property(_srt.CBytePerfMon_msTimeStamp_get, _srt.CBytePerfMon_msTimeStamp_set)
    pktSentTotal = property(_srt.CBytePerfMon_pktSentTotal_get, _srt.CBytePerfMon_pktSentTotal_set)
    pktRecvTotal = property(_srt.CBytePerfMon_pktRecvTotal_get, _srt.CBytePerfMon_pktRecvTotal_set)
    pktSndLossTotal = property(_srt.CBytePerfMon_pktSndLossTotal_get, _srt.CBytePerfMon_pktSndLossTotal_set)
    pktRcvLossTotal = property(_srt.CBytePerfMon_pktRcvLossTotal_get, _srt.CBytePerfMon_pktRcvLossTotal_set)
    pktRetransTotal = property(_srt.CBytePerfMon_pktRetransTotal_get, _srt.CBytePerfMon_pktRetransTotal_set)
    pktSentACKTotal = property(_srt.CBytePerfMon_pktSentACKTotal_get, _srt.CBytePerfMon_pktSentACKTotal_set)
    pktRecvACKTotal = property(_srt.CBytePerfMon_pktRecvACKTotal_get, _srt.CBytePerfMon_pktRecvACKTotal_set)
    pktSentNAKTotal = property(_srt.CBytePerfMon_pktSentNAKTotal_get, _srt.CBytePerfMon_pktSentNAKTotal_set)
    pktRecvNAKTotal = property(_srt.CBytePerfMon_pktRecvNAKTotal_get, _srt.CBytePerfMon_pktRecvNAKTotal_set)
    usSndDurationTotal = property(_srt.CBytePerfMon_usSndDurationTotal_get, _srt.CBytePerfMon_usSndDurationTotal_set)
    pktSndDropTotal = property(_srt.CBytePerfMon_pktSndDropTotal_get, _srt.CBytePerfMon_pktSndDropTotal_set)
    pktRcvDropTotal = property(_srt.CBytePerfMon_pktRcvDropTotal_get, _srt.CBytePerfMon_pktRcvDropTotal_set)
    pktRcvUndecryptTotal = property(_srt.CBytePerfMon_pktRcvUndecryptTotal_get, _srt.CBytePerfMon_pktRcvUndecryptTotal_set)
    byteSentTotal = property(_srt.CBytePerfMon_byteSentTotal_get, _srt.CBytePerfMon_byteSentTotal_set)
    byteRecvTotal = property(_srt.CBytePerfMon_byteRecvTotal_get, _srt.CBytePerfMon_byteRecvTotal_set)
    byteRetransTotal = property(_srt.CBytePerfMon_byteRetransTotal_get, _srt.CBytePerfMon_byteRetransTotal_set)
    byteSndDropTotal = property(_srt.CBytePerfMon_byteSndDropTotal_get, _srt.CBytePerfMon_byteSndDropTotal_set)
    byteRcvDropTotal = property(_srt.CBytePerfMon_byteRcvDropTotal_get, _srt.CBytePerfMon_byteRcvDropTotal_set)
    byteRcvUndecryptTotal = property(_srt.CBytePerfMon_byteRcvUndecryptTotal_get, _srt.CBytePerfMon_byteRcvUndecryptTotal_set)
    pktSent = property(_srt.CBytePerfMon_pktSent_get, _srt.CBytePerfMon_pktSent_set)
    pktRecv = property(_srt.CBytePerfMon_pktRecv_get, _srt.CBytePerfMon_pktRecv_set)
    pktSndLoss = property(_srt.CBytePerfMon_pktSndLoss_get, _srt.CBytePerfMon_pktSndLoss_set)
    pktRcvLoss = property(_srt.CBytePerfMon_pktRcvLoss_get, _srt.CBytePerfMon_pktRcvLoss_set)
    pktRetrans = property(_srt.CBytePerfMon_pktRetrans_get, _srt.CBytePerfMon_pktRetrans_set)
    pktRcvRetrans = property(_srt.CBytePerfMon_pktRcvRetrans_get, _srt.CBytePerfMon_pktRcvRetrans_set)
    pktSentACK = property(_srt.CBytePerfMon_pktSentACK_get, _srt.CBytePerfMon_pktSentACK_set)
    pktRecvACK = property(_srt.CBytePerfMon_pktRecvACK_get, _srt.CBytePerfMon_pktRecvACK_set)
    pktSentNAK = property(_srt.CBytePerfMon_pktSentNAK_get, _srt.CBytePerfMon_pktSentNAK_set)
    pktRecvNAK = property(_srt.CBytePerfMon_pktRecvNAK_get, _srt.CBytePerfMon_pktRecvNAK_set)
    mbpsSendRate = property(_srt.CBytePerfMon_mbpsSendRate_get, _srt.CBytePerfMon_mbpsSendRate_set)
    mbpsRecvRate = property(_srt.CBytePerfMon_mbpsRecvRate_get, _srt.CBytePerfMon_mbpsRecvRate_set)
    usSndDuration = property(_srt.CBytePerfMon_usSndDuration_get, _srt.CBytePerfMon_usSndDuration_set)
    pktReorderDistance = property(_srt.CBytePerfMon_pktReorderDistance_get, _srt.CBytePerfMon_pktReorderDistance_set)
    pktRcvAvgBelatedTime = property(_srt.CBytePerfMon_pktRcvAvgBelatedTime_get, _srt.CBytePerfMon_pktRcvAvgBelatedTime_set)
    pktRcvBelated = property(_srt.CBytePerfMon_pktRcvBelated_get, _srt.CBytePerfMon_pktRcvBelated_set)
    pktSndDrop = property(_srt.CBytePerfMon_pktSndDrop_get, _srt.CBytePerfMon_pktSndDrop_set)
    pktRcvDrop = property(_srt.CBytePerfMon_pktRcvDrop_get, _srt.CBytePerfMon_pktRcvDrop_set)
    pktRcvUndecrypt = property(_srt.CBytePerfMon_pktRcvUndecrypt_get, _srt.CBytePerfMon_pktRcvUndecrypt_set)
    byteSent = property(_srt.CBytePerfMon_byteSent_get, _srt.CBytePerfMon_byteSent_set)
    byteRecv = property(_srt.CBytePerfMon_byteRecv_get, _srt.CBytePerfMon_byteRecv_set)
    byteRetrans = property(_srt.CBytePerfMon_byteRetrans_get, _srt.CBytePerfMon_byteRetrans_set)
    byteSndDrop = property(_srt.CBytePerfMon_byteSndDrop_get, _srt.CBytePerfMon_byteSndDrop_set)
    byteRcvDrop = property(_srt.CBytePerfMon_byteRcvDrop_get, _srt.CBytePerfMon_byteRcvDrop_set)
    byteRcvUndecrypt = property(_srt.CBytePerfMon_byteRcvUndecrypt_get, _srt.CBytePerfMon_byteRcvUndecrypt_set)
    usPktSndPeriod = property(_srt.CBytePerfMon_usPktSndPeriod_get, _srt.CBytePerfMon_usPktSndPeriod_set)
    pktFlowWindow = property(_srt.CBytePerfMon_pktFlowWindow_get, _srt.CBytePerfMon_pktFlowWindow_set)
    pktCongestionWindow = property(_srt.CBytePerfMon_pktCongestionWindow_get, _srt.CBytePerfMon_pktCongestionWindow_set)
    pktFlightSize = property(_srt.CBytePerfMon_pktFlightSize_get, _srt.CBytePerfMon_pktFlightSize_set)
    msRTT = property(_srt.CBytePerfMon_msRTT_get, _srt.CBytePerfMon_msRTT_set)
    mbpsBandwidth = property(_srt.CBytePerfMon_mbpsBandwidth_get, _srt.CBytePerfMon_mbpsBandwidth_set)
    byteAvailSndBuf = property(_srt.CBytePerfMon_byteAvailSndBuf_get, _srt.CBytePerfMon_byteAvailSndBuf_set)
    byteAvailRcvBuf = property(_srt.CBytePerfMon_byteAvailRcvBuf_get, _srt.CBytePerfMon_byteAvailRcvBuf_set)
    mbpsMaxBW = property(_srt.CBytePerfMon_mbpsMaxBW_get, _srt.CBytePerfMon_mbpsMaxBW_set)
    byteMSS = property(_srt.CBytePerfMon_byteMSS_get, _srt.CBytePerfMon_byteMSS_set)
    pktSndBuf = property(_srt.CBytePerfMon_pktSndBuf_get, _srt.CBytePerfMon_pktSndBuf_set)
    byteSndBuf = property(_srt.CBytePerfMon_byteSndBuf_get, _srt.CBytePerfMon_byteSndBuf_set)
    msSndBuf = property(_srt.CBytePerfMon_msSndBuf_get, _srt.CBytePerfMon_msSndBuf_set)
    msSndTsbPdDelay = property(_srt.CBytePerfMon_msSndTsbPdDelay_get, _srt.CBytePerfMon_msSndTsbPdDelay_set)
    pktRcvBuf = property(_srt.CBytePerfMon_pktRcvBuf_get, _srt.CBytePerfMon_pktRcvBuf_set)
    byteRcvBuf = property(_srt.CBytePerfMon_byteRcvBuf_get, _srt.CBytePerfMon_byteRcvBuf_set)
    msRcvBuf = property(_srt.CBytePerfMon_msRcvBuf_get, _srt.CBytePerfMon_msRcvBuf_set)
    msRcvTsbPdDelay = property(_srt.CBytePerfMon_msRcvTsbPdDelay_get, _srt.CBytePerfMon_msRcvTsbPdDelay_set)
    pktSndFilterExtraTotal = property(_srt.CBytePerfMon_pktSndFilterExtraTotal_get, _srt.CBytePerfMon_pktSndFilterExtraTotal_set)
    pktRcvFilterExtraTotal = property(_srt.CBytePerfMon_pktRcvFilterExtraTotal_get, _srt.CBytePerfMon_pktRcvFilterExtraTotal_set)
    pktRcvFilterSupplyTotal = property(_srt.CBytePerfMon_pktRcvFilterSupplyTotal_get, _srt.CBytePerfMon_pktRcvFilterSupplyTotal_set)
    pktRcvFilterLossTotal = property(_srt.CBytePerfMon_pktRcvFilterLossTotal_get, _srt.CBytePerfMon_pktRcvFilterLossTotal_set)
    pktSndFilterExtra = property(_srt.CBytePerfMon_pktSndFilterExtra_get, _srt.CBytePerfMon_pktSndFilterExtra_set)
    pktRcvFilterExtra = property(_srt.CBytePerfMon_pktRcvFilterExtra_get, _srt.CBytePerfMon_pktRcvFilterExtra_set)
    pktRcvFilterSupply = property(_srt.CBytePerfMon_pktRcvFilterSupply_get, _srt.CBytePerfMon_pktRcvFilterSupply_set)
    pktRcvFilterLoss = property(_srt.CBytePerfMon_pktRcvFilterLoss_get, _srt.CBytePerfMon_pktRcvFilterLoss_set)
    pktReorderTolerance = property(_srt.CBytePerfMon_pktReorderTolerance_get, _srt.CBytePerfMon_pktReorderTolerance_set)

    def __init__(self):
        _srt.CBytePerfMon_swiginit(self, _srt.new_CBytePerfMon())
    __swig_destroy__ = _srt.delete_CBytePerfMon

# Register CBytePerfMon in _srt:
_srt.CBytePerfMon_swigregister(CBytePerfMon)
cvar = _srt.cvar
SRTGROUP_MASK = cvar.SRTGROUP_MASK
SRT_LIVE_DEF_PLSIZE = cvar.SRT_LIVE_DEF_PLSIZE
SRT_LIVE_MAX_PLSIZE = cvar.SRT_LIVE_MAX_PLSIZE
SRT_LIVE_DEF_LATENCY_MS = cvar.SRT_LIVE_DEF_LATENCY_MS

MJ_UNKNOWN = _srt.MJ_UNKNOWN
MJ_SUCCESS = _srt.MJ_SUCCESS
MJ_SETUP = _srt.MJ_SETUP
MJ_CONNECTION = _srt.MJ_CONNECTION
MJ_SYSTEMRES = _srt.MJ_SYSTEMRES
MJ_FILESYSTEM = _srt.MJ_FILESYSTEM
MJ_NOTSUP = _srt.MJ_NOTSUP
MJ_AGAIN = _srt.MJ_AGAIN
MJ_PEERERROR = _srt.MJ_PEERERROR
MN_NONE = _srt.MN_NONE
MN_TIMEOUT = _srt.MN_TIMEOUT
MN_REJECTED = _srt.MN_REJECTED
MN_NORES = _srt.MN_NORES
MN_SECURITY = _srt.MN_SECURITY
MN_CONNLOST = _srt.MN_CONNLOST
MN_NOCONN = _srt.MN_NOCONN
MN_THREAD = _srt.MN_THREAD
MN_MEMORY = _srt.MN_MEMORY
MN_SEEKGFAIL = _srt.MN_SEEKGFAIL
MN_READFAIL = _srt.MN_READFAIL
MN_SEEKPFAIL = _srt.MN_SEEKPFAIL
MN_WRITEFAIL = _srt.MN_WRITEFAIL
MN_ISBOUND = _srt.MN_ISBOUND
MN_ISCONNECTED = _srt.MN_ISCONNECTED
MN_INVAL = _srt.MN_INVAL
MN_SIDINVAL = _srt.MN_SIDINVAL
MN_ISUNBOUND = _srt.MN_ISUNBOUND
MN_NOLISTEN = _srt.MN_NOLISTEN
MN_ISRENDEZVOUS = _srt.MN_ISRENDEZVOUS
MN_ISRENDUNBOUND = _srt.MN_ISRENDUNBOUND
MN_INVALMSGAPI = _srt.MN_INVALMSGAPI
MN_INVALBUFFERAPI = _srt.MN_INVALBUFFERAPI
MN_BUSY = _srt.MN_BUSY
MN_XSIZE = _srt.MN_XSIZE
MN_EIDINVAL = _srt.MN_EIDINVAL
MN_EEMPTY = _srt.MN_EEMPTY
MN_WRAVAIL = _srt.MN_WRAVAIL
MN_RDAVAIL = _srt.MN_RDAVAIL
MN_XMTIMEOUT = _srt.MN_XMTIMEOUT
MN_CONGESTION = _srt.MN_CONGESTION
SRT_EUNKNOWN = _srt.SRT_EUNKNOWN
SRT_SUCCESS = _srt.SRT_SUCCESS
SRT_ECONNSETUP = _srt.SRT_ECONNSETUP
SRT_ENOSERVER = _srt.SRT_ENOSERVER
SRT_ECONNREJ = _srt.SRT_ECONNREJ
SRT_ESOCKFAIL = _srt.SRT_ESOCKFAIL
SRT_ESECFAIL = _srt.SRT_ESECFAIL
SRT_ECONNFAIL = _srt.SRT_ECONNFAIL
SRT_ECONNLOST = _srt.SRT_ECONNLOST
SRT_ENOCONN = _srt.SRT_ENOCONN
SRT_ERESOURCE = _srt.SRT_ERESOURCE
SRT_ETHREAD = _srt.SRT_ETHREAD
SRT_ENOBUF = _srt.SRT_ENOBUF
SRT_EFILE = _srt.SRT_EFILE
SRT_EINVRDOFF = _srt.SRT_EINVRDOFF
SRT_ERDPERM = _srt.SRT_ERDPERM
SRT_EINVWROFF = _srt.SRT_EINVWROFF
SRT_EWRPERM = _srt.SRT_EWRPERM
SRT_EINVOP = _srt.SRT_EINVOP
SRT_EBOUNDSOCK = _srt.SRT_EBOUNDSOCK
SRT_ECONNSOCK = _srt.SRT_ECONNSOCK
SRT_EINVPARAM = _srt.SRT_EINVPARAM
SRT_EINVSOCK = _srt.SRT_EINVSOCK
SRT_EUNBOUNDSOCK = _srt.SRT_EUNBOUNDSOCK
SRT_ENOLISTEN = _srt.SRT_ENOLISTEN
SRT_ERDVNOSERV = _srt.SRT_ERDVNOSERV
SRT_ERDVUNBOUND = _srt.SRT_ERDVUNBOUND
SRT_EINVALMSGAPI = _srt.SRT_EINVALMSGAPI
SRT_EINVALBUFFERAPI = _srt.SRT_EINVALBUFFERAPI
SRT_EDUPLISTEN = _srt.SRT_EDUPLISTEN
SRT_ELARGEMSG = _srt.SRT_ELARGEMSG
SRT_EINVPOLLID = _srt.SRT_EINVPOLLID
SRT_EPOLLEMPTY = _srt.SRT_EPOLLEMPTY
SRT_EASYNCFAIL = _srt.SRT_EASYNCFAIL
SRT_EASYNCSND = _srt.SRT_EASYNCSND
SRT_EASYNCRCV = _srt.SRT_EASYNCRCV
SRT_ETIMEOUT = _srt.SRT_ETIMEOUT
SRT_ECONGEST = _srt.SRT_ECONGEST
SRT_EPEERERR = _srt.SRT_EPEERERR
SRT_REJ_UNKNOWN = _srt.SRT_REJ_UNKNOWN
SRT_REJ_SYSTEM = _srt.SRT_REJ_SYSTEM
SRT_REJ_PEER = _srt.SRT_REJ_PEER
SRT_REJ_RESOURCE = _srt.SRT_REJ_RESOURCE
SRT_REJ_ROGUE = _srt.SRT_REJ_ROGUE
SRT_REJ_BACKLOG = _srt.SRT_REJ_BACKLOG
SRT_REJ_IPE = _srt.SRT_REJ_IPE
SRT_REJ_CLOSE = _srt.SRT_REJ_CLOSE
SRT_REJ_VERSION = _srt.SRT_REJ_VERSION
SRT_REJ_RDVCOOKIE = _srt.SRT_REJ_RDVCOOKIE
SRT_REJ_BADSECRET = _srt.SRT_REJ_BADSECRET
SRT_REJ_UNSECURE = _srt.SRT_REJ_UNSECURE
SRT_REJ_MESSAGEAPI = _srt.SRT_REJ_MESSAGEAPI
SRT_REJ_CONGESTION = _srt.SRT_REJ_CONGESTION
SRT_REJ_FILTER = _srt.SRT_REJ_FILTER
SRT_REJ_GROUP = _srt.SRT_REJ_GROUP
SRT_REJ__SIZE = _srt.SRT_REJ__SIZE
SRT_LOGFA_GENERAL = _srt.SRT_LOGFA_GENERAL
SRT_LOGFA_BSTATS = _srt.SRT_LOGFA_BSTATS
SRT_LOGFA_CONTROL = _srt.SRT_LOGFA_CONTROL
SRT_LOGFA_DATA = _srt.SRT_LOGFA_DATA
SRT_LOGFA_TSBPD = _srt.SRT_LOGFA_TSBPD
SRT_LOGFA_REXMIT = _srt.SRT_LOGFA_REXMIT
SRT_LOGFA_HAICRYPT = _srt.SRT_LOGFA_HAICRYPT
SRT_LOGFA_CONGEST = _srt.SRT_LOGFA_CONGEST
SRT_LOGFA_LASTNONE = _srt.SRT_LOGFA_LASTNONE
SRT_KM_S_UNSECURED = _srt.SRT_KM_S_UNSECURED
SRT_KM_S_SECURING = _srt.SRT_KM_S_SECURING
SRT_KM_S_SECURED = _srt.SRT_KM_S_SECURED
SRT_KM_S_NOSECRET = _srt.SRT_KM_S_NOSECRET
SRT_KM_S_BADSECRET = _srt.SRT_KM_S_BADSECRET
SRT_EPOLL_OPT_NONE = _srt.SRT_EPOLL_OPT_NONE
SRT_EPOLL_IN = _srt.SRT_EPOLL_IN
SRT_EPOLL_OUT = _srt.SRT_EPOLL_OUT
SRT_EPOLL_ERR = _srt.SRT_EPOLL_ERR
SRT_EPOLL_CONNECT = _srt.SRT_EPOLL_CONNECT
SRT_EPOLL_ACCEPT = _srt.SRT_EPOLL_ACCEPT
SRT_EPOLL_UPDATE = _srt.SRT_EPOLL_UPDATE
SRT_EPOLL_ET = _srt.SRT_EPOLL_ET
SRT_EPOLL_ENABLE_EMPTY = _srt.SRT_EPOLL_ENABLE_EMPTY
SRT_EPOLL_ENABLE_OUTPUTCHECK = _srt.SRT_EPOLL_ENABLE_OUTPUTCHECK
SRT_GTYPE_UNDEFINED = _srt.SRT_GTYPE_UNDEFINED
SRT_GTYPE_BROADCAST = _srt.SRT_GTYPE_BROADCAST
SRT_GTYPE_BACKUP = _srt.SRT_GTYPE_BACKUP
SRT_GTYPE_BALANCING = _srt.SRT_GTYPE_BALANCING
SRT_GTYPE_MULTICAST = _srt.SRT_GTYPE_MULTICAST
SRT_GTYPE__END = _srt.SRT_GTYPE__END

def srt_startup():
    return _srt.srt_startup()

def srt_cleanup():
    return _srt.srt_cleanup()

def srt_socket(arg1, arg2, arg3):
    return _srt.srt_socket(arg1, arg2, arg3)

def srt_create_socket():
    return _srt.srt_create_socket()
class SRT_SOCKGROUPDATA(object):
    thisown = property(lambda x: x.this.own(), lambda x, v: x.this.own(v), doc="The membership flag")
    __repr__ = _swig_repr
    id = property(_srt.SRT_SOCKGROUPDATA_id_get, _srt.SRT_SOCKGROUPDATA_id_set)
    status = property(_srt.SRT_SOCKGROUPDATA_status_get, _srt.SRT_SOCKGROUPDATA_status_set)
    result = property(_srt.SRT_SOCKGROUPDATA_result_get, _srt.SRT_SOCKGROUPDATA_result_set)
    srcaddr = property(_srt.SRT_SOCKGROUPDATA_srcaddr_get, _srt.SRT_SOCKGROUPDATA_srcaddr_set)
    peeraddr = property(_srt.SRT_SOCKGROUPDATA_peeraddr_get, _srt.SRT_SOCKGROUPDATA_peeraddr_set)
    priority = property(_srt.SRT_SOCKGROUPDATA_priority_get, _srt.SRT_SOCKGROUPDATA_priority_set)

    def __init__(self):
        _srt.SRT_SOCKGROUPDATA_swiginit(self, _srt.new_SRT_SOCKGROUPDATA())
    __swig_destroy__ = _srt.delete_SRT_SOCKGROUPDATA

# Register SRT_SOCKGROUPDATA in _srt:
_srt.SRT_SOCKGROUPDATA_swigregister(SRT_SOCKGROUPDATA)
MN_ISSTREAM = cvar.MN_ISSTREAM
MN_ISDGRAM = cvar.MN_ISDGRAM
SRT_EISSTREAM = cvar.SRT_EISSTREAM
SRT_EISDGRAM = cvar.SRT_EISDGRAM
SRT_INVALID_SOCK = cvar.SRT_INVALID_SOCK
SRT_ERROR = cvar.SRT_ERROR


def srt_create_group(arg1):
    return _srt.srt_create_group(arg1)

def srt_include(socket, group):
    return _srt.srt_include(socket, group)

def srt_exclude(socket):
    return _srt.srt_exclude(socket)

def srt_groupof(socket):
    return _srt.srt_groupof(socket)

def srt_group_data(socketgroup, output, inoutlen):
    return _srt.srt_group_data(socketgroup, output, inoutlen)

def srt_group_configure(socketgroup, str):
    return _srt.srt_group_configure(socketgroup, str)

def srt_bind(u, name, namelen):
    return _srt.srt_bind(u, name, namelen)

def srt_bind_acquire(u, sys_udp_sock):
    return _srt.srt_bind_acquire(u, sys_udp_sock)

def srt_bind_peerof(u, sys_udp_sock):
    return _srt.srt_bind_peerof(u, sys_udp_sock)

def srt_listen(u, backlog):
    return _srt.srt_listen(u, backlog)

def srt_accept(u, addr, addrlen):
    return _srt.srt_accept(u, addr, addrlen)

def srt_accept_bond(listeners, lsize, msTimeOut):
    return _srt.srt_accept_bond(listeners, lsize, msTimeOut)

def srt_listen_callback(lsn, hook_fn, hook_opaque):
    return _srt.srt_listen_callback(lsn, hook_fn, hook_opaque)

def srt_connect(u, name, namelen):
    return _srt.srt_connect(u, name, namelen)

def srt_connect_debug(u, name, namelen, forced_isn):
    return _srt.srt_connect_debug(u, name, namelen, forced_isn)

def srt_connect_bind(u, source, target, len):
    return _srt.srt_connect_bind(u, source, target, len)

def srt_rendezvous(u, local_name, local_namelen, remote_name, remote_namelen):
    return _srt.srt_rendezvous(u, local_name, local_namelen, remote_name, remote_namelen)

def srt_prepare_endpoint(src, adr, namelen):
    return _srt.srt_prepare_endpoint(src, adr, namelen)

def srt_connect_group(group, name, arraysize):
    return _srt.srt_connect_group(group, name, arraysize)

def srt_close(u):
    return _srt.srt_close(u)

def srt_getpeername(u, name, namelen):
    return _srt.srt_getpeername(u, name, namelen)

def srt_getsockname(u, name, namelen):
    return _srt.srt_getsockname(u, name, namelen)

def srt_getsockopt(u, level, optname, optval, optlen):
    return _srt.srt_getsockopt(u, level, optname, optval, optlen)

def srt_setsockopt(u, level, optname, optval, optlen):
    return _srt.srt_setsockopt(u, level, optname, optval, optlen)

def srt_getsockflag(u, opt, optval, optlen):
    return _srt.srt_getsockflag(u, opt, optval, optlen)

def srt_setsockflag(u, opt, optval, optlen):
    return _srt.srt_setsockflag(u, opt, optval, optlen)
class SRT_MSGCTRL(object):
    thisown = property(lambda x: x.this.own(), lambda x, v: x.this.own(v), doc="The membership flag")
    __repr__ = _swig_repr
    flags = property(_srt.SRT_MSGCTRL_flags_get, _srt.SRT_MSGCTRL_flags_set)
    msgttl = property(_srt.SRT_MSGCTRL_msgttl_get, _srt.SRT_MSGCTRL_msgttl_set)
    inorder = property(_srt.SRT_MSGCTRL_inorder_get, _srt.SRT_MSGCTRL_inorder_set)
    boundary = property(_srt.SRT_MSGCTRL_boundary_get, _srt.SRT_MSGCTRL_boundary_set)
    srctime = property(_srt.SRT_MSGCTRL_srctime_get, _srt.SRT_MSGCTRL_srctime_set)
    pktseq = property(_srt.SRT_MSGCTRL_pktseq_get, _srt.SRT_MSGCTRL_pktseq_set)
    msgno = property(_srt.SRT_MSGCTRL_msgno_get, _srt.SRT_MSGCTRL_msgno_set)
    grpdata = property(_srt.SRT_MSGCTRL_grpdata_get, _srt.SRT_MSGCTRL_grpdata_set)
    grpdata_size = property(_srt.SRT_MSGCTRL_grpdata_size_get, _srt.SRT_MSGCTRL_grpdata_size_set)

    def __init__(self):
        _srt.SRT_MSGCTRL_swiginit(self, _srt.new_SRT_MSGCTRL())
    __swig_destroy__ = _srt.delete_SRT_MSGCTRL

# Register SRT_MSGCTRL in _srt:
_srt.SRT_MSGCTRL_swigregister(SRT_MSGCTRL)


def srt_msgctrl_init(mctrl):
    return _srt.srt_msgctrl_init(mctrl)

def srt_send(u, buf, len):
    return _srt.srt_send(u, buf, len)

def srt_sendmsg(u, buf, len, ttl, inorder):
    return _srt.srt_sendmsg(u, buf, len, ttl, inorder)

def srt_sendmsg2(u, buf, len, mctrl):
    return _srt.srt_sendmsg2(u, buf, len, mctrl)

def srt_recv(u, buf, len):
    return _srt.srt_recv(u, buf, len)

def srt_recvmsg(u, buf, len):
    return _srt.srt_recvmsg(u, buf, len)

def srt_recvmsg2(u, buf, len, mctrl):
    return _srt.srt_recvmsg2(u, buf, len, mctrl)
SRT_DEFAULT_SENDFILE_BLOCK = _srt.SRT_DEFAULT_SENDFILE_BLOCK
SRT_DEFAULT_RECVFILE_BLOCK = _srt.SRT_DEFAULT_RECVFILE_BLOCK

def srt_sendfile(u, path, offset, size, block):
    return _srt.srt_sendfile(u, path, offset, size, block)

def srt_recvfile(u, path, offset, size, block):
    return _srt.srt_recvfile(u, path, offset, size, block)

def srt_getlasterror_str():
    return _srt.srt_getlasterror_str()

def srt_getlasterror(errno_loc):
    return _srt.srt_getlasterror(errno_loc)

def srt_strerror(code, errnoval):
    return _srt.srt_strerror(code, errnoval)

def srt_clearlasterror():
    return _srt.srt_clearlasterror()

def srt_bstats(u, perf, clear):
    return _srt.srt_bstats(u, perf, clear)

def srt_bistats(u, perf, clear, instantaneous):
    return _srt.srt_bistats(u, perf, clear, instantaneous)

def srt_getsockstate(u):
    return _srt.srt_getsockstate(u)

def srt_epoll_create():
    return _srt.srt_epoll_create()

def srt_epoll_clear_usocks(eid):
    return _srt.srt_epoll_clear_usocks(eid)

def srt_epoll_add_usock(eid, u, events):
    return _srt.srt_epoll_add_usock(eid, u, events)

def srt_epoll_add_ssock(eid, s, events):
    return _srt.srt_epoll_add_ssock(eid, s, events)

def srt_epoll_remove_usock(eid, u):
    return _srt.srt_epoll_remove_usock(eid, u)

def srt_epoll_remove_ssock(eid, s):
    return _srt.srt_epoll_remove_ssock(eid, s)

def srt_epoll_update_usock(eid, u, events):
    return _srt.srt_epoll_update_usock(eid, u, events)

def srt_epoll_update_ssock(eid, s, events):
    return _srt.srt_epoll_update_ssock(eid, s, events)

def srt_epoll_wait(eid, readfds, rnum, writefds, wnum, msTimeOut, lrfds, lrnum, lwfds, lwnum):
    return _srt.srt_epoll_wait(eid, readfds, rnum, writefds, wnum, msTimeOut, lrfds, lrnum, lwfds, lwnum)
class SRT_EPOLL_EVENT(object):
    thisown = property(lambda x: x.this.own(), lambda x, v: x.this.own(v), doc="The membership flag")
    __repr__ = _swig_repr
    fd = property(_srt.SRT_EPOLL_EVENT_fd_get, _srt.SRT_EPOLL_EVENT_fd_set)
    events = property(_srt.SRT_EPOLL_EVENT_events_get, _srt.SRT_EPOLL_EVENT_events_set)

    def __init__(self):
        _srt.SRT_EPOLL_EVENT_swiginit(self, _srt.new_SRT_EPOLL_EVENT())
    __swig_destroy__ = _srt.delete_SRT_EPOLL_EVENT

# Register SRT_EPOLL_EVENT in _srt:
_srt.SRT_EPOLL_EVENT_swigregister(SRT_EPOLL_EVENT)
SRT_SEQNO_NONE = cvar.SRT_SEQNO_NONE
SRT_MSGNO_NONE = cvar.SRT_MSGNO_NONE
SRT_MSGNO_CONTROL = cvar.SRT_MSGNO_CONTROL
SRT_MSGTTL_INF = cvar.SRT_MSGTTL_INF
srt_msgctrl_default = cvar.srt_msgctrl_default


def srt_epoll_uwait(eid, fdsSet, fdsSize, msTimeOut):
    return _srt.srt_epoll_uwait(eid, fdsSet, fdsSize, msTimeOut)

def srt_epoll_set(eid, flags):
    return _srt.srt_epoll_set(eid, flags)

def srt_epoll_release(eid):
    return _srt.srt_epoll_release(eid)

def srt_setloglevel(ll):
    return _srt.srt_setloglevel(ll)

def srt_addlogfa(fa):
    return _srt.srt_addlogfa(fa)

def srt_dellogfa(fa):
    return _srt.srt_dellogfa(fa)

def srt_resetlogfa(fara, fara_size):
    return _srt.srt_resetlogfa(fara, fara_size)

def srt_setloghandler(opaque, handler):
    return _srt.srt_setloghandler(opaque, handler)

def srt_setlogflags(flags):
    return _srt.srt_setlogflags(flags)

def srt_getsndbuffer(sock, blocks, bytes):
    return _srt.srt_getsndbuffer(sock, blocks, bytes)

def srt_getrejectreason(sock):
    return _srt.srt_getrejectreason(sock)

def srt_rejectreason_str(id):
    return _srt.srt_rejectreason_str(id)

def srt_getversion():
    return _srt.srt_getversion()

srt_rejectreason_msg = cvar.srt_rejectreason_msg

