#pragma once
#include "services/station_workflow.hpp"
namespace opentag::services {
inline const char* workflow_guidance(WorkflowStage stage) {
  switch (stage) {
    case WorkflowStage::awaiting_spool: return "Place an OpenPrintTag spool";
    case WorkflowStage::waiting_for_stable_weight: return "Waiting for stable weight";
    case WorkflowStage::resolving_spool: return "Finding Spoolman spool";
    case WorkflowStage::spool_resolution_unavailable: return "Spoolman unavailable; check Settings";
    case WorkflowStage::spool_not_found: return "No spool matched; confirm ID in web UI";
    case WorkflowStage::spool_selection_required: return "Multiple matches; confirm spool in web UI";
    case WorkflowStage::spool_ready: return "Spool ready; select a printer toolhead";
    case WorkflowStage::assignment_complete: return "Assignment verified by readback";
  }
  return "Checking spool";
}
}
