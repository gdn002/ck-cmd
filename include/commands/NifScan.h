// Command Base
#include <commands/CommandBase.h>

#include <filesystem>

#include <niflib.h>
#include <obj\NiObject.h>
#include <obj\BSFadeNode.h>

#include <objDecl.cpp>
#include <field_visitor.h>
#include <interfaces\typed_visitor.h>

//hierarchy
#include <obj/NiTimeController.h>
#include <obj/NiExtraData.h>
#include <obj/NiCollisionObject.h>
#include <obj/NiProperty.h>
#include <obj/NiDynamicEffect.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <fstream>
#include <utility>

#include <bitset>
#include <filesystem>

namespace fs = std::experimental::filesystem;

static const fs::path nif_scan_in = ".\\resources\\in";
static const fs::path nif_scan_err = "D:\\git\\ck-cmd\\resources\\err";

namespace ckcmd {
	namespace nifscan {

        class NifScan : public CommandBase
        {
            REGISTER_COMMAND_HEADER(NifScan)

        private:
            NifScan();
            virtual ~NifScan();

        public:
            virtual string GetName() const;
            virtual string GetHelp() const;
            virtual string GetHelpShort() const;

        protected:
            virtual bool InternalRunCommand(map<string, docopt::value> parsedArgs);
        };

		using namespace Niflib;

		typedef bitset<12> bsx_flags_t;
		bsx_flags_t calculateSkyrimBSXFlags(const vector<NiObjectRef>& blocks, const NifInfo& info);

		class SingleChunkFlagVerifier : public RecursiveFieldVisitor<SingleChunkFlagVerifier> {

			int n_collisions = 0;
			int n_phantoms = 0;
			int n_constraints = 0;
			bool hasBranches = false;
			bool branchesResult = true;
			set<pair<bhkEntity*, bhkEntity*>>& entitiesPair;
			const NifInfo& this_info;
			set<NiObject*>& alreadyVisitedNodes;

		public:
			bool singleChunkVerified = false;

			SingleChunkFlagVerifier(NiObject& data, const NifInfo& info) :
				RecursiveFieldVisitor(*this, info), this_info(info), alreadyVisitedNodes(set<NiObject*>()), entitiesPair(set<pair<bhkEntity*, bhkEntity*>>())
			{
				data.accept(*this, info);

				bool singlechain = false;
				if (n_collisions - n_constraints == 1) {
					singlechain = true;
					singleChunkVerified = true;
				}
				if (n_phantoms > 0 && (singlechain || n_collisions == 0)) {
					singleChunkVerified = true;
				}

				if (hasBranches) {
					if (n_collisions == 0 && n_phantoms == 0)
						singleChunkVerified = singleChunkVerified || branchesResult;
					else
						singleChunkVerified = singleChunkVerified && branchesResult;
				}
			}

			SingleChunkFlagVerifier(NiObject& data, const NifInfo& info, set<NiObject*>& alreadyVisitedNodes, set<pair<bhkEntity*, bhkEntity*>>& entitiesPair) :
				RecursiveFieldVisitor(*this, info), this_info(info), alreadyVisitedNodes(alreadyVisitedNodes), entitiesPair(entitiesPair)
			{
				data.accept(*this, info);

				bool singlechain = false;
				if (n_collisions - n_constraints == 1) {
					singlechain = true;
					singleChunkVerified = true;
				}
				if (n_phantoms > 0 && (singlechain || n_collisions == 0)) {
					singleChunkVerified = true;
				}
				if (hasBranches) {
					if (n_collisions == 0 && n_phantoms == 0)
						singleChunkVerified = singleChunkVerified || branchesResult;
					else
						singleChunkVerified = singleChunkVerified && branchesResult;
				}

				if (n_phantoms == 0 && n_collisions == 0)
					singleChunkVerified = true;
			}

			template<class T>
			inline void visit_object(T& obj) {
				NiObject* ptr = (NiObject*)&obj;
				if (alreadyVisitedNodes.insert(ptr).second) {
					NiObjectRef ref = DynamicCast<NiObject>(ptr);
					if (ref->IsSameType(NiSwitchNode::TYPE)) {
						branchesResult = false;
						hasBranches = true;
						bool singleResult = true;
						NiSwitchNodeRef ref = DynamicCast<NiSwitchNode>(ptr);
						for (NiAVObjectRef child : ref->GetChildren()) {
							bool result = SingleChunkFlagVerifier(*child, this_info, alreadyVisitedNodes, entitiesPair).singleChunkVerified;
							singleResult = singleResult && result;
						}
						branchesResult = branchesResult || singleResult;
					}

					if (ref->IsDerivedType(bhkSPCollisionObject::TYPE)) {
						n_phantoms++;
					}
					if (ref->IsDerivedType(bhkCollisionObject::TYPE)) {
						n_collisions++;
					}

					if (ref->IsDerivedType(bhkConstraint::TYPE)) {
						bhkConstraintRef cref = DynamicCast<bhkConstraint>(ref);
						std::pair<bhkEntity*, bhkEntity*> p;
						p.first = *cref->GetEntities().begin();
						p.second = *(++cref->GetEntities().begin());
						if (entitiesPair.insert(p).second)
							n_constraints++;
					}
				}
			}

			template<class T>
			inline void visit_compound(T& obj) {}

			template<class T>
			inline void visit_field(T& obj) {}

		};

		class MarkerBranchVisitor : public RecursiveFieldVisitor<MarkerBranchVisitor> {
			set<NiObject*>& alreadyVisitedNodes;
			bool insideBranch = false;
			const NifInfo& this_info;
		public:
			bool marker = false;

			MarkerBranchVisitor(NiObject& data, const NifInfo& info) :
				RecursiveFieldVisitor(*this, info), alreadyVisitedNodes(set<NiObject*>()), this_info(info)
			{
				data.accept(*this, info);
			}

			//verifies that markers are not inside branches

			MarkerBranchVisitor(NiObject& data, const NifInfo& info, bool insideBranch, set<NiObject*>& alreadyVisitedNodes) :
				RecursiveFieldVisitor(*this, info), insideBranch(insideBranch), alreadyVisitedNodes(alreadyVisitedNodes), this_info(info)
			{
				data.accept(*this, info);
			}

			template<class T>
			inline void visit_object(T& obj) {
				NiObject* ptr = (NiObject*)&obj;
				if (alreadyVisitedNodes.insert(ptr).second) {
					NiObjectRef ref = DynamicCast<NiObject>(ptr);
					if (ref->IsSameType(NiSwitchNode::TYPE)) {
						NiSwitchNodeRef ref = DynamicCast<NiSwitchNode>(ptr);
						//For whatever reason, seems like the EditorMarker flag actually is just taking into account the first branch,
						//which is the active one by default. If the editor is in the other, like into the money bag, the flag is reset
						if (!ref->GetChildren().empty()) {
							marker = MarkerBranchVisitor(*ref->GetChildren()[0], this_info, false, alreadyVisitedNodes).marker;
						}
						//mark all as no marker, could be actually done iteratively on the blocks list instead of visiting
						for (NiAVObjectRef child : ref->GetChildren()) {
							MarkerBranchVisitor(*child, this_info, true, alreadyVisitedNodes);
						}
						//This construct is used for ambient fish to be taken and disapper, they are flagged
						//if (ref->GetChildren().size() == 2 && ref->GetChildren()[1]->IsDerivedType(NiNode::TYPE)) {
						//	NiNodeRef ref = DynamicCast<NiNode>(ref->GetChildren()[1]);
						//	if (ref->GetName().empty() && ref->GetChildren().empty())
						//		marker = MarkerBranchVisitor(*ref->GetChildren()[0], this_info, false, alreadyVisitedNodes).marker;
						//	else
						//		//mark all as no marker, could be actually done iteratively on the blocks list instead of visiting
						//		for (NiAVObjectRef child : ref->GetChildren()) {
						//			MarkerBranchVisitor(*child, this_info, true, alreadyVisitedNodes);
						//		}
						//}
						//else {
						//	//mark all as no marker, could be actually done iteratively on the blocks list instead of visiting
						//	for (NiAVObjectRef child : ref->GetChildren()) {
						//		MarkerBranchVisitor(*child, this_info, true, alreadyVisitedNodes);
						//	}
						//}
					}

					if (ref->IsSameType(BSOrderedNode::TYPE)) {
						BSOrderedNodeRef ref = DynamicCast<BSOrderedNode>(ptr);
						for (NiAVObjectRef child : ref->GetChildren()) {
							MarkerBranchVisitor(*child, this_info, true, alreadyVisitedNodes);
						}
					}

					if (!insideBranch && ref->IsDerivedType(NiObjectNET::TYPE)) {
						NiObjectNETRef node = DynamicCast<NiObjectNET>(ref);
						if (node->GetName().find("EditorMarker") != string::npos)
							marker = true;
					}

				}
			}

			template<class T>
			inline void visit_compound(T& obj) {}

			template<class T>
			inline void visit_field(T& obj) {}
		};

	}
}
