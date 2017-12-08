#ifndef POS_SALE_OBJECT_H_
#define POS_SALE_OBJECT_H_

#include "BaseRTAObject.h"

namespace supernode {
	class PosSaleObject : public BaseRTAObject {
		public:
		bool Init(const RTA_TransactionRecordBase& src) override;
		bool GetSaleStatus(const rpc_command::POS_GET_SALE_STATUS::request& in, rpc_command::POS_GET_SALE_STATUS::response& out);
		bool PoSTRSigned(const rpc_command::POS_TR_SIGNED::request& in, rpc_command::POS_TR_SIGNED::response& out);

        bool PosRejectSale(const rpc_command::POS_REJECT_SALE::request &in, rpc_command::POS_REJECT_SALE::response &out);
        bool AuthWalletRejectPay(const rpc_command::WALLET_REJECT_PAY::request &in, rpc_command::WALLET_REJECT_PAY::response &out);

		protected:
		string GeneratePaymentID();

		protected:
		unsigned m_Signs = 0;
        NTransactionStatus m_Status = NTransactionStatus::None;
	};
}

#endif /* POS_SALE_OBJECT_H_ */
