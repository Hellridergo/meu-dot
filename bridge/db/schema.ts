import { sqliteTable, text, integer, real, primaryKey, index } from "drizzle-orm/sqlite-core";

export const voices = sqliteTable("voices", {
  id: text("id").primaryKey(),
  message: text("message").notNull(),
  seconds: real("seconds").notNull(),
  created: integer("created").notNull(),
});
export const cards = sqliteTable("cards", {
  id: text("id").primaryKey(),
  message: text("message").notNull(),
  decision: integer("decision").notNull(),
  created: integer("created").notNull(),
  delivered: integer("delivered"),
  deliveryStage: text("delivery_stage"),
  recordingId: text("recording_id"),
  answer: text("answer"),
  answered: integer("answered"),
});
export const subscriptions = sqliteTable("subscriptions", {
  id: text("id").primaryKey(),
  name: text("name").notNull(),
  filterId: text("filter_id").notNull(),
  sealed: text("sealed").notNull(),
  created: integer("created").notNull(),
  expires: integer("expires").notNull(),
});
export const deliveries = sqliteTable("deliveries", {
  sub: text("sub").notNull(),
  event: text("event").notNull(),
  name: text("name").notNull(),
  data: text("data").notNull(),
  created: integer("created").notNull(),
  phase: text("phase").notNull().default("pending"),
  tries: integer("tries").notNull().default(0),
  due: integer("due").notNull().default(0),
  lease: text("lease"),
  attempted: integer("attempted"),
  sent: integer("sent"),
  http: integer("http"),
}, t => [primaryKey({columns:[t.sub,t.event]}),index("delivery_due").on(t.phase,t.due)]);
export const mobile = sqliteTable("mobile", {
  id: integer("id").primaryKey(),
  lastSeen: integer("last_seen").notNull(),
  client: text("client").notNull(),
});
// One redacted result per event, visible only to the owner. Never store callback
// addresses, request bodies, signing material, or user speech in this table.
export const subscriptionChecks = sqliteTable("subscription_checks", {
  name: text("name").primaryKey(),
  at: integer("at").notNull(),
  stage: text("stage").notNull(),
  http: integer("http"),
});
