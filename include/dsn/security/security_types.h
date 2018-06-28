/**
 * Autogenerated by Thrift Compiler (0.9.3)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#ifndef security_TYPES_H
#define security_TYPES_H

#include <iosfwd>

#include <thrift/Thrift.h>
#include <thrift/TApplicationException.h>
#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TTransport.h>

#include <thrift/cxxfunctional.h>


namespace dsn { namespace security {

struct negotiation_status {
  enum type {
    INVALID = 0,
    SASL_SUCC = 1,
    SASL_INITIATE = 2,
    SASL_CHALLENGE = 3,
    SASL_RESPONSE = 4,
    SASL_AUTH_FAIL = 5
  };
};

extern const std::map<int, const char*> _negotiation_status_VALUES_TO_NAMES;

class negotiation_message;

typedef struct _negotiation_message__isset {
  _negotiation_message__isset() : status(false), msg(false) {}
  bool status :1;
  bool msg :1;
} _negotiation_message__isset;

class negotiation_message {
 public:

  negotiation_message(const negotiation_message&);
  negotiation_message(negotiation_message&&);
  negotiation_message& operator=(const negotiation_message&);
  negotiation_message& operator=(negotiation_message&&);
  negotiation_message() : status((negotiation_status::type)0), msg() {
  }

  virtual ~negotiation_message() throw();
  negotiation_status::type status;
  std::string msg;

  _negotiation_message__isset __isset;

  void __set_status(const negotiation_status::type val);

  void __set_msg(const std::string& val);

  bool operator == (const negotiation_message & rhs) const
  {
    if (!(status == rhs.status))
      return false;
    if (!(msg == rhs.msg))
      return false;
    return true;
  }
  bool operator != (const negotiation_message &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const negotiation_message & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

  virtual void printTo(std::ostream& out) const;
};

void swap(negotiation_message &a, negotiation_message &b);

inline std::ostream& operator<<(std::ostream& out, const negotiation_message& obj)
{
  obj.printTo(out);
  return out;
}

}} // namespace

#endif