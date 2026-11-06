# The configuration that tools/check-tofu.sh and tools/check-data.sh run OpenTofu against. The variables exist so that
# check-data.sh can change one thing at a time (-var name=value).

variable "prefix" {
  type    = string
  default = "wp"
}
variable "full_name" {
  type    = string
  default = "full"
}
variable "full_size" {
  type    = number
  default = 7
}
variable "full_ratio" {
  type    = number
  default = 0.5
}
variable "full_enabled" {
  type    = bool
  default = true
}
variable "full_description" {
  type    = string
  default = "everything set"
}

# Only the one required attribute: every other attribute gets its default.
resource "workaround_example" "basic" {
  name = "basic"
}

# Every kind of attribute: string, bool, whole and fractional numbers, map, list, set, sensitive.
resource "workaround_example" "full" {
  name        = var.full_name
  description = var.full_description
  enabled     = var.full_enabled
  size        = var.full_size
  ratio       = var.full_ratio
  tags        = { team = "core", env = "test" }
  items       = ["a", "b", "c"]
  labels      = ["x", "y", "x"] # a set: the duplicate disappears
  secret      = "s3cret"
}

# The name is another resource's computed id, so it is unknown while planning.
resource "workaround_example" "chained" {
  name = workaround_example.basic.id
}

output "basic_id" {
  value = workaround_example.basic.id
}
output "full_summary" {
  value = workaround_example.full.summary
}
output "chained_summary" {
  value = workaround_example.chained.summary
}
