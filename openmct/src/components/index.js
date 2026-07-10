import { defineEventLog } from '@/components/event-log.js';
import { defineRawInspector } from '@/components/raw-inspector.js';
import { defineStateBadge } from '@/components/state-badge.js';
import { defineTelemetryCard } from '@/components/telemetry-card.js';

export function defineDashboardComponents() {
  defineStateBadge();
  defineTelemetryCard();
  defineEventLog();
  defineRawInspector();
}
